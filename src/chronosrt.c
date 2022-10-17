#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <asm/prctl.h>
#include <bpf/libbpf.h>
#include <chronos/chronosrt.h>
#include <log.h>
#include <ns.h>
#include <pairing_heap.h>
#include <pthread.h>
#include <sched.h>
#include <signal.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdnoreturn.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <trace_switch.h>
#include <trace_switch.skel.h>
#include <unistd.h>
#include <vruntime.h>

// Task's state
enum chronosrt_task_state {
	// Default scheduling state
	NORMAL,
	// exit_thread() has been called and thread is waiting for a goahead from scheduler to terminate
	EXIT_THREAD_PENDING,
	// clone() has been called and thread is waiting for scheduler to pick up new thread
	THREAD_SPAWN_PENDING,
	// sched_yield() has been called and thread is waiting for the scheduler to ack reschedule
	YIELD_PENDING,
	// Thread has not been scheduled yet
	START_PENDING,
};

// Task data
struct chronosrt_tcb {
	// Virtual runtime thread statistics
	struct chronos_vruntime vruntime;
	// Run-queue node hook
	chronos_pairing_heap_node_t rq_node;
	union {
		// Pointer to this for GS <-> normal address space conversion
		struct chronosrt_tcb *self;
		// Pointer to the next TCB object in tcb's pool
		struct chronosrt_tcb *next;
	};
	// CPU task is meant to run on
	struct chronosrt_vcpu *vcpu;
	// Task's state
	_Atomic enum chronosrt_task_state state;
	// Pointer to the TCB to be enqueued
	struct chronosrt_tcb *thread_spawn_tcb;
	// Cumulative yield() vruntime difference
	size_t yield_vruntime_diff;
	// Thread's vruntime on creation
	size_t creation_vruntime;
	// Virtual runtime snapshot. Updated on every simulation cycle and used for scheduling decisions
	size_t vruntime_snapshot;
	// Thread handle
	union {
		// Pthread handle (used in pthread mode)
		pthread_t pthread;
		// Thread's tid default mode
		pid_t tid;
	} handle;
	// True if the thread is a pthread
	bool pthread_mode;
};

// Per-vCPU data
struct chronosrt_vcpu {
	// CPU set that only includes this CPU's physical counterpart
	chronos_cpuset_t set;
	// Simulation time on this core
	size_t sim_time;
	// Runqueue for this CPU
	chronos_pairing_heap_t rq;
	// Currently running task (if any)
	struct chronosrt_tcb *current;
	// Timeslice start timestamp (in the timeline of the current thread)
	size_t current_timeslice_start;
	// Number of runnable tasks
	atomic_size_t runnable_tasks;
	// Minimum vruntime among all tasks in the runqueue
	size_t minimum_vruntime;
	// Maximum vruntime among all tasks in the runqueue
	size_t maximum_vruntime;
	// Simulation time snapshot used for scheduling decisions
	size_t sim_time_snapshot;
	// vCPU state
	enum {
		// Capable of running tasks
		// NOTE: state == ONLINE does not mean that CPU is currently running a task, the correct way to check for this
		// would be state == ONLINE && current != NULL
		// NOTE: simulation time for this vCPU only advances in this state
		ONLINE,
		// Frozen due to being way ahead in virtual time. "current" points to the task that CPU will run once its online
		// again
		FROZEN,
		// Blocked, i.e. thread running on this vCPU has entered critical section. Scheduler can't touch vcpu->current
		// while vcpu->state == BLOCKED. It follows that scheduler can't preempt currently running thread
		BLOCKED,
	} state;
	// Set to true if vCPU has been frozen (to allow other vCPUs to catch up)
	bool frozen;
};

// TCB size
const size_t chronosrt_tcb_size = sizeof(struct chronosrt_tcb);

// Copy of the config
static struct chronosrt_config cfg;

// Per-CPU data
struct chronosrt_vcpu *vcpus;

#define ID(vcpu) (vcpu - vcpus)

// Number of the virtual CPUs
size_t vcpus_count;

// RT priorities for normal threads and scheduler
static int scheduler_priority;
static int barrier_priority;
static int normal_priority;

// Scheduler thread information
static pthread_t scheduler_pthread;       // Pthread handle of the scheduler
static pthread_t barrier_pthread;         // Pthread handle of the barrier thread
static chronos_cpuset_t scheduler_cpuset; // CPU set with only scheduler's core enabled
static atomic_int scheduler_goahead = 0;  // Set to 1 to signal that main thread is ready

// Simulation statistics
atomic_size_t gvt = 0;                 // Global simulation (virtual) time
atomic_size_t loops_done = 0;          // Simulation loop iterations count
atomic_size_t sim_start_timestamp = 0; // Simulation start timestamp

// Current timestamp
size_t current_real_time = 0;

// BPF maps
struct ring_buffer *ev_ringbuffer; // Ringbuffer with events from BPF probe
struct trace_switch_bpf *bpf_skel; // BPF skeleton

////// RT helpers //////

// Move a given thread to the SCHED_FIFO scheduling class
static void chronosrt_fifo_bump_tid(pid_t tid, int prio) {
	struct sched_param param;
	param.sched_priority = prio;
	chronos_assert_false(sched_setscheduler(tid, SCHED_FIFO, &param));
}

// Move a given thread to the SCHED_FIFO scheduling class
static void chronosrt_fifo_bump_pthread(pthread_t pthread, int prio) {
	struct sched_param param;
	param.sched_priority = prio;
	chronos_assert_false(pthread_setschedparam(pthread, SCHED_FIFO, &param));
}

// Query RT priorities to be used by the runtime
static void chronosrt_priorities_query(void) {
	int rt_min = sched_get_priority_min(SCHED_FIFO);
	chronos_assert(rt_min >= 0);
	int rt_max = sched_get_priority_max(SCHED_FIFO);
	chronos_assert(rt_max >= 0);
	// Use three lowest real-time priorities available
	normal_priority = rt_min;
	barrier_priority = rt_min + 1;
	scheduler_priority = rt_min + 2;
	chronos_assert(scheduler_priority <= rt_max);
}

////// Signal management //////

// Disable signals on the current thread
void chronosrt_disable_signals() {
	sigset_t mask;
	// Some signals stay on to catch fatal errors, but others should be handled by application threads
	chronos_assert_false(sigfillset(&mask));
	chronos_assert_false(sigdelset(&mask, SIGSEGV));
	chronos_assert_false(sigdelset(&mask, SIGBUS));
	chronos_assert_false(sigdelset(&mask, SIGFPE));
	chronos_assert_false(sigprocmask(SIG_SETMASK, &mask, NULL));
}

////// TCB helpers //////

inline static void chronosrt_set_tcb_ptr(struct chronosrt_tcb *tcb) {
	// Notify BPF probe that this thread is now part of the framework
	pid_t key = gettid();
	chronos_tcb_addr_t val = (chronos_tcb_addr_t)tcb;
	chronos_assert_false(
	    bpf_map__update_elem(bpf_skel->maps.tcb_ptrs, &key, sizeof(key), &val, sizeof(val), BPF_NOEXIST));
#if defined(__x86_64)
	chronos_assert_false(syscall(SYS_arch_prctl, ARCH_SET_GS, tcb));
	tcb->self = tcb;
#else
#error Unsupported architecture
#endif
}

inline static struct chronosrt_tcb *chronosrt_get_tcb_ptr() {
#if defined(__x86_64)
	__seg_gs struct chronosrt_tcb *tcb_ptr = (__seg_gs void *)0;
	return tcb_ptr->self;
#else
#error Unsupported architecture
#endif
}

// Public API wrapper for chronosrt_get_tcb_ptr() that does not expose TCB type
void *chronosrt_get_tcb(void) {
	return chronosrt_get_tcb_ptr();
}

////// Thread's creation //////

// Pick vCPU for the new task
static struct chronosrt_vcpu *chronosrt_best_vcpu(void) {
	size_t min_tasks = atomic_load(&vcpus[0].runnable_tasks);
	struct chronosrt_vcpu *min_tasks_cpu = vcpus;
	for (size_t i = 1; i < vcpus_count; ++i) {
		struct chronosrt_vcpu *vcpu = vcpus + i;
		size_t tasks = atomic_load(&vcpu->runnable_tasks);
		if (tasks < min_tasks) {
			min_tasks_cpu = vcpu;
			min_tasks = tasks;
		}
	}
	atomic_fetch_add(&min_tasks_cpu->runnable_tasks, 1);
	return min_tasks_cpu;
}

// Wait for scheduler to migrate current thread from START_PENDING to NORMAL state
static void chronosrt_wait_for_normal(struct chronosrt_tcb *tcb) {
	size_t loads_count = 0;
	while (atomic_load(&tcb->state) == START_PENDING) {
		sched_yield();
		loads_count++;
	}
}

// Initialize a thread control block (usually called before clone() or pthread_create()) in place
// NOTE: memory from backing_memory to backing_memory + chronosrt_tcb_size is claimed by the runtime
void *chronosrt_init_new_tcb(void *backing_memory) {
	chronos_assert(backing_memory);
	if (cfg.runtime_mode == NOP) {
		return NULL; // Don't care
	}
	struct chronosrt_tcb *tcb = backing_memory;
	tcb->self = tcb;
	tcb->vcpu = chronosrt_best_vcpu();
	tcb->yield_vruntime_diff = 0;
	atomic_store(&tcb->state, START_PENDING);
	return tcb;
}

// Set tcb's thread handle to a given thread ID
inline static void chronosrt_set_handle_tid(struct chronosrt_tcb *tcb, pid_t tid) {
	tcb->handle.tid = tid;
	tcb->pthread_mode = false;
}

// Set tcb's thread handle to a given pthread
inline static void chronosrt_set_handle_pthread(struct chronosrt_tcb *tcb, pthread_t pthread) {
	tcb->handle.pthread = pthread;
	tcb->pthread_mode = true;
}

////// Runqueue management ///////

// Get TCB of the thread with the lowest vruntime in the runqueue
static struct chronosrt_tcb *chronosrt_rq_get_min(struct chronosrt_vcpu *vcpu) {
	chronos_pairing_heap_node_t *node = chronos_pairing_heap_get_min(&vcpu->rq);
	if (node == NULL) {
		return NULL;
	}
	return (void *)((uintptr_t)node - offsetof(struct chronosrt_tcb, rq_node));
}

// Enqueue TCB in the runqueue (vruntime needs to be comitted)
static void chronosrt_rq_enqueue(struct chronosrt_vcpu *vcpu, struct chronosrt_tcb *tcb) {
	chronos_pairing_heap_t *heap = &vcpu->rq;
	chronos_pairing_heap_node_t *node = &tcb->rq_node;
	node->key = tcb->vruntime_snapshot + tcb->yield_vruntime_diff;
	// Make sure to update maximum_vruntime field used for yields
	if (node->key > vcpu->maximum_vruntime) {
		vcpu->maximum_vruntime = node->key;
	}
	chronos_pairing_heap_insert(heap, node);
	// Same for min vruntime
	vcpu->minimum_vruntime = chronosrt_rq_get_min(vcpu)->rq_node.key;
	chronos_rt_log("Runqueue for vCPU #%zu: Enqueued new task %p, the key is %zu, new min/max keys: %zu, %zu", ID(vcpu),
	               tcb, node->key, vcpu->minimum_vruntime, vcpu->maximum_vruntime);
}

// Remove TCB of the thread with the lowest vruntime from the runqueue
static struct chronosrt_tcb *chronosrt_rq_dequeue(struct chronosrt_vcpu *vcpu) {
	chronos_pairing_heap_node_t *node = chronos_pairing_heap_del_min(&vcpu->rq);
	if (node == NULL) {
		chronos_rt_log("Runqueue for vCPU #%zu: Try to dequeue, but the queue is empty =(", ID(vcpu));
		return NULL;
	}
	// No need to update maximum_vruntime, since we got the smallest element
	// We do need to update the minimum_vruntime however
	struct chronosrt_tcb *min = chronosrt_rq_get_min(vcpu);
	if (min != NULL) {
		vcpu->minimum_vruntime = min->rq_node.key;
	}
	struct chronosrt_tcb *tcb = (void *)((uintptr_t)node - offsetof(struct chronosrt_tcb, rq_node));
	chronos_rt_log(
	    "Runqueue for vCPU #%zu: Dequeued task with the least key %p, the key is %zu, new min/max keys: %zu, %zu",
	    ID(vcpu), tcb, node->key, vcpu->minimum_vruntime, vcpu->maximum_vruntime);
	return tcb;
}

////// Scheduling functions //////

// Suspend thread
static void chronosrt_suspend(struct chronosrt_tcb *tcb) {
	if (tcb->pthread_mode) {
		chronos_cpuset_set_pthread_affinity(tcb->handle.pthread, scheduler_cpuset);
	} else {
		chronos_cpuset_set_tid_affinity(tcb->handle.tid, scheduler_cpuset);
	}
}

// Resume thread
static void chronosrt_resume(struct chronosrt_tcb *tcb) {
	struct chronosrt_vcpu *vcpu = tcb->vcpu;
	if (tcb->pthread_mode) {
		chronos_cpuset_set_pthread_affinity(tcb->handle.pthread, vcpu->set);
	} else {
		chronos_cpuset_set_tid_affinity(tcb->handle.tid, vcpu->set);
	}
}

// Switch thread to the FIFO scheduling policy
static void chronosrt_make_realtime(struct chronosrt_tcb *tcb) {
	if (tcb->pthread_mode) {
		chronosrt_fifo_bump_pthread(tcb->handle.pthread, normal_priority);
	} else {
		chronosrt_fifo_bump_tid(tcb->handle.tid, normal_priority);
	}
}

// Stop task from running on a given virtual CPU
// PRE: current task exists
static void chronosrt_stop_running_tasks(struct chronosrt_vcpu *vcpu, bool enqueue, bool suspend) {
	chronos_rt_log("Suspending current(%p) on vCPU #%zu", vcpu->current, ID(vcpu));
	// Verify precondition
	chronos_assert(vcpu->current);
	size_t current_vruntime = vcpu->current->vruntime_snapshot;
	if (suspend) {
		// Suspend task
		chronosrt_suspend(vcpu->current);
	}
	if (enqueue) {
		// Enqueue current task into the runqueue
		chronosrt_rq_enqueue(vcpu, vcpu->current);
	}
	// New task has not been picked yet
	vcpu->current = NULL;
	// Update simulation time for this core
	vcpu->sim_time += current_vruntime - vcpu->current_timeslice_start;
	// Rescheduling comes with a price
	vcpu->sim_time += cfg.reschedule_penalty;
}

// Resume, picking a new task to run
static bool chronosrt_cont_running_tasks(struct chronosrt_vcpu *vcpu) {
	// There should be no current task
	chronos_assert_false(vcpu->current);
	// Pick a new task from the runqueue
	vcpu->current = chronosrt_rq_dequeue(vcpu);
	if (!vcpu->current) {
		// No task to run, entering idle
		return false;
	}
	chronos_rt_log("Resuming new current(%p) on vCPU #%zu", vcpu->current, ID(vcpu));
	vcpu->current_timeslice_start = vcpu->current->vruntime_snapshot;
	// Advance last update timestamp (this is needed to account for the time between timeslices)
	chronos_vruntime_update_real(&vcpu->current->vruntime, current_real_time);
	// Resume task
	chronosrt_resume(vcpu->current);
	// LOOK INTO THIS
	if (atomic_load(&vcpu->current->state) == START_PENDING) {
		atomic_store(&vcpu->current->state, NORMAL);
	}
	return true;
}

// Freeze vCPU
static void chronosrt_freeze_vcpu(struct chronosrt_vcpu *vcpu) {
	chronos_assert(vcpu->state == ONLINE);
	chronos_rt_log("Freezing vCPU #%zu", ID(vcpu));
	vcpu->state = FROZEN;
	size_t new_vruntime = vcpu->current->vruntime_snapshot;
	vcpu->sim_time += new_vruntime - vcpu->current_timeslice_start;
	chronosrt_suspend(vcpu->current);
}

// Unfreeze vCPU
static void chronosrt_unfreeze_vcpu(struct chronosrt_vcpu *vcpu) {
	chronos_assert(vcpu->state == FROZEN);
	chronos_rt_log("Unfreezing vCPU #%zu", ID(vcpu));
	vcpu->state = ONLINE;
	vcpu->current_timeslice_start = vcpu->current->vruntime_snapshot;
	chronos_vruntime_update_real(&vcpu->current->vruntime, current_real_time);
	chronosrt_resume(vcpu->current);
}

// Trigger preemption on vCPU
static void chronosrt_preempt_on_cpu(struct chronosrt_vcpu *vcpu) {
	chronosrt_stop_running_tasks(vcpu, true, true);
	chronosrt_cont_running_tasks(vcpu);
}

// Commit vruntime on vCPU
static void chronosrt_commit_vruntime_on_vcpu(struct chronosrt_vcpu *vcpu) {
	if (vcpu->current == NULL || vcpu->state != ONLINE) {
		return;
	}
	vcpu->current->vruntime_snapshot = chronos_vruntime_get(&vcpu->current->vruntime, current_real_time);
}

// Check if the timeslice of the current task has expired
static void chronosrt_check_for_timeslice_expiration(struct chronosrt_vcpu *vcpu) {
	if (vcpu->current == NULL || vcpu->state != ONLINE) {
		return;
	}
	size_t current_vruntime = vcpu->current->vruntime_snapshot;
	size_t timeslice_elapsed = current_vruntime - vcpu->current_timeslice_start;
	current_vruntime += vcpu->current->yield_vruntime_diff;
	// Check if there is any other thread we can switch to
	struct chronosrt_tcb *alternative = chronosrt_rq_get_min(vcpu);
	if (alternative == NULL) {
		return;
	}
	size_t alternative_vruntime = alternative->rq_node.key;
	// Only switch if both conditions hold
	// 1. alternative_vruntime < current_vruntime (i.e. it would be more fair to run the best alternative)
	// 2. timeslice_elapsed > cfg.min_local_timeslice (we want to avoid jumping around way to often, rescheduling is
	// not free after all)
	if (current_vruntime > alternative_vruntime && timeslice_elapsed > cfg.min_local_timeslice) {
		chronos_rt_log("Local scheduler for vCPU #%zu: ACKed expired timeslice (current(%p)'s vruntime is %zuns, "
		               "alternative(%p)'s vruntime is %zuns)",
		               ID(vcpu), vcpu->current, current_vruntime, alternative, alternative_vruntime);
		chronosrt_preempt_on_cpu(vcpu);
	}
}

// Check for pending local calls from current task on a given vCPU
// NOTE: local calls are the main mechanism via which tasks talk to a scheduler to terminate, spawn new threads, etc
static void chronosrt_check_for_local_calls(struct chronosrt_vcpu *vcpu) {
	if (vcpu->current == NULL || vcpu->state != ONLINE) {
		return;
	}
	struct chronosrt_tcb *current = vcpu->current;
	enum chronosrt_task_state state = atomic_load(&current->state);
	if (state == NORMAL) {
		return;
	}
	chronos_rt_log("Local call handler for vCPU #%zu: Picked up local call request", ID(vcpu));
	switch (state) {
	case EXIT_THREAD_PENDING:
		chronos_rt_log("Blocking vCPU #%zu", ID(vcpu));
		// Tell the load balancer this CPU now has one task less
		atomic_fetch_sub(&vcpu->runnable_tasks, 1);
		// Switch vCPU to the blocked state
		vcpu->state = BLOCKED;
		// Pointer to current is of no use now (thread can free TCB at any point once local call is returned), might as
		// well set it to NULL to catch some bugs
		vcpu->current = NULL;
		// Allow thread to continue in non-interruptable state
		atomic_store(&current->state, NORMAL);
		break;
	case THREAD_SPAWN_PENDING: {
		// Get new TCB to be enqueued
		struct chronosrt_tcb *new_tcb = current->thread_spawn_tcb;
		// Set new thread's affinity to the scheduler mask to prevent it from running
		chronosrt_suspend(new_tcb);
		// Switch to real-time scheduling policy
		chronosrt_make_realtime(new_tcb);
		// Set vruntime to minimum_vruntime of the core. This should prevent new thread from starving others
		size_t target_vcpu_min_vruntime = new_tcb->vcpu->minimum_vruntime;
		// NOTE: we also need to compare min_vruntime for the core with the runtime of the current task on this core
		// There are two cases where not doing this would be unfair
		// 1. If current thread is substantially behind all threads in the runqueue
		// 2. If current thread is the only runnable thread (i.e. runqueue is empty) and hence min_vruntime has not
		// been updated in ages
		if (new_tcb->vcpu->current != NULL) {
			struct chronosrt_tcb *new_tcb_vcpu_current = new_tcb->vcpu->current;
			// NOTE: we assume that vruntime was comitted relatively recently (during stage 0 of the main loop)
			size_t new_tcb_vcpu_current_vruntime = new_tcb_vcpu_current->vruntime_snapshot;
			new_tcb_vcpu_current_vruntime += new_tcb_vcpu_current->yield_vruntime_diff;
			if (chronosrt_rq_get_min(new_tcb->vcpu) == NULL ||
			    target_vcpu_min_vruntime > new_tcb_vcpu_current_vruntime) {
				target_vcpu_min_vruntime = new_tcb_vcpu_current_vruntime;
			}
		}
		chronos_vruntime_init(&new_tcb->vruntime, 0, cfg.barriers_deadline, target_vcpu_min_vruntime);
		new_tcb->creation_vruntime = target_vcpu_min_vruntime;
		new_tcb->vruntime_snapshot = target_vcpu_min_vruntime;
		// Add TCB to the runqueue
		chronosrt_rq_enqueue(new_tcb->vcpu, new_tcb);
		// Wake CPU from idle if needed
		if (new_tcb->vcpu->current == NULL) {
			new_tcb->vcpu->sim_time = atomic_load(&gvt);
			chronosrt_cont_running_tasks(new_tcb->vcpu);
		}
		// Allow the parent to continue
		atomic_store(&current->state, NORMAL);
		chronos_rt_log(
		    "Local call handler for vCPU #%zu: ACKed spawn of thread %p, initial vruntime is %zu, new thread "
		    "will be running on vCPU %zu",
		    ID(vcpu), new_tcb, target_vcpu_min_vruntime, ID(new_tcb->vcpu));
		break;
	}
	case YIELD_PENDING:
		// Check if there is any other task running that we can switch to
		if (chronosrt_rq_get_min(vcpu) != NULL) {
			// Other runnable tasks are present, its preemption time
			// Suspend current thread without enqueuing it into the runqueue. We need to do it ourselves as we update
			// vruntime
			chronosrt_stop_running_tasks(vcpu, false, true);
			// Setting current's vruntime to vcpu->maximum_vruntime will give all other tasks in the runqueue a chance
			// to run
			size_t current_vruntime = current->vruntime_snapshot;
			size_t diff = vcpu->maximum_vruntime + 1 - current_vruntime;
			// NOTE: current->yield_vruntime_diff is used to calculate the difference between real time thread has been
			// running for and one stored in vruntime. To get one from another, we need to simply subtract/add
			// yield_vruntime_diff
			current->yield_vruntime_diff += diff;
			// Add current to the runqueue
			chronosrt_rq_enqueue(vcpu, current);
			// Allow CPU to pick a new task
			chronosrt_cont_running_tasks(vcpu);
			chronos_rt_log("Local call handler for vCPU #%zu: forcefully preempted from thread %p to thread %p",
			               ID(vcpu), current, vcpu->current);
		}
		// Allow suspended thread to continue as soon as scheduler picks it up again
		atomic_store(&current->state, NORMAL);
		break;
	default:
		// Nothing for us to do
		break;
	}
}

// Compute simulation time for a given vCPU
static bool chronosrt_compute_sim_time(struct chronosrt_vcpu *vcpu) {
	if (vcpu->state == ONLINE && vcpu->current == NULL) {
		// Simulation time can only be computed for non-idle CPUs
		return false;
	} else if (vcpu->state != ONLINE) {
		// Simulation time stopped at vcpu->sim_time
		vcpu->sim_time_snapshot = vcpu->sim_time;
		return true;
	}
	chronos_assert(vcpu->current != NULL);
	// We start computing simulation time with simulation time at the beginning of the timeslice
	size_t result = vcpu->sim_time;
	// Now we need to add the difference between timeslice start timestamp and current timestamp
	size_t current_vruntime = vcpu->current->vruntime_snapshot;
	size_t vruntime_diff = current_vruntime - vcpu->current_timeslice_start;
	result += vruntime_diff;
	// Store the result in the snapshot variable
	vcpu->sim_time_snapshot = result;
	return true;
}

// Transfer vCPU from BLOCKED state to ONLINE state after thread exit notification from BPF probe
static void chronosrt_unblock_on_exit(struct chronosrt_vcpu *vcpu) {
	// TODO: thread termination probably should not be free (it currently is). The problem is that vcpu->current is
	// gone, so we can't calculate new virtual time of the terminated thread. Is there any way to handle this?

	// For now the only thing we can do is just wake up the CPU and tell it to pick a new task to run
	chronos_rt_log("Unblocking vCPU #%zu", ID(vcpu));
	vcpu->state = ONLINE;
	chronosrt_cont_running_tasks(vcpu);
}

// Handle an event from BPF probe
static int chronosrt_handle_trace_switch_event(void *ctx, void *data, size_t sz) {
	(void)ctx; // Unused
	chronos_assert(sizeof(struct chronos_switch_event) == sz);
	struct chronos_switch_event *ev = data;
	// Switch on event type
	switch (ev->ty) {
	case CHRONOS_SWITCH_EV_EXIT: {
		// Get pointer to the vCPU task runs on
		struct chronosrt_vcpu *vcpu = vcpus + ev->vcpu;
		// Tasks can only terminate in blocked state
		chronos_assert(vcpu->state == BLOCKED);
		// Unblock the core after exit
		chronosrt_unblock_on_exit(vcpu);
	} break;
	default:
		// Unknown event type
		chronos_assert(false);
	}
	return 1;
}

// Finish initialization on the main thread
static void chronosrt_scheduler_start(void *param) {
	struct chronosrt_tcb *main_thread_tcb = param;

	// We are not going to handle application signals
	chronosrt_disable_signals();

	// Wait for the main thread to finish init
	while (!atomic_load(&scheduler_goahead)) {
		sched_yield();
	}

	// Suspend main thread until we are ready to schedule it
	chronosrt_suspend(main_thread_tcb);

	// Tell main thread it can continue as soon as we resume it
	atomic_store(&main_thread_tcb->state, NORMAL);

	// Update simulation start timestamp
	size_t start_ts = chronos_ns_get_monotonic();
	current_real_time = start_ts;
	atomic_store(&sim_start_timestamp, start_ts);

	// Start CPU main thread is on
	chronosrt_cont_running_tasks(main_thread_tcb->vcpu);

	chronos_rt_log("Scheduler bootstrap done");
}

// Scheduler routine
static void *chronosrt_scheduler_thread(void *param) {
	chronosrt_scheduler_start(param);

	// Main loop
	while (true) {
		// Ensure that all vruntimes are comitted before making scheduling decisions
		current_real_time = chronos_ns_get_monotonic();
		for (size_t i = 0; i < vcpus_count; ++i) {
			chronosrt_commit_vruntime_on_vcpu(vcpus + i);
		}
		// Step 0. For each CPU, see if we need to reschedule
		for (size_t i = 0; i < vcpus_count; ++i) {
			chronosrt_check_for_timeslice_expiration(vcpus + i);
		}
		// Step 1. Run pending local calls
		for (size_t i = 0; i < vcpus_count; ++i) {
			chronosrt_check_for_local_calls(vcpus + i);
		}
		// Step 2. Compute new global virtual time
		size_t new_gvt = SIZE_MAX;
		for (size_t i = 0; i < vcpus_count; ++i) {
			if (chronosrt_compute_sim_time(vcpus + i)) {
				size_t this_cpu_sim_time = vcpus[i].sim_time_snapshot;
				if (this_cpu_sim_time < new_gvt) {
					new_gvt = this_cpu_sim_time;
				}
			}
		}
		// Step 3. If new_gvt is SIZE_MAX, no task is running and we don't have events yet, so we can exit
		if (new_gvt == SIZE_MAX) {
			_exit(EXIT_SUCCESS);
		}
		// Step 4. Commit new GVT
		atomic_store(&gvt, new_gvt);
		// Step 5. Determine which vCPUs should be (un)frozen
		for (size_t i = 0; i < vcpus_count; ++i) {
			struct chronosrt_vcpu *vcpu = vcpus + i;
			// Check criteria for suspending/resuming vCPU from freeze
			// NOTE: we need to prevent idle CPUs from getting suspended
			if ((vcpu->state == FROZEN) && vcpu->sim_time_snapshot < (new_gvt + cfg.resume_lag_allowance)) {
				// Frozen vCPU with vruntime mismatch less than resume lag allowance
				// Wake vCPU up
				chronosrt_unfreeze_vcpu(vcpu);
			} else if (vcpu->state == ONLINE && vcpu->current != NULL &&
			           vcpu->sim_time_snapshot > (new_gvt + cfg.suspend_lag_allowance)) {
				// vCPU with vruntime mismatch more than suspend lag allowance
				// Freeze vCPU
				chronosrt_freeze_vcpu(vcpu);
			}
		}
		// Step 5. Consume new events in the ring buffer
		ring_buffer__consume(ev_ringbuffer);

		atomic_fetch_add(&loops_done, 1);
	}

	return NULL;
}

// Spawn a real-time thread on scheduler core
static pthread_t chronosrt_spawn_on_scheduler_core(void *(*routine)(void *), void *arg) {
	pthread_t result;
	chronos_assert_false(pthread_create(&result, NULL, routine, arg));
	chronos_assert_false(pthread_detach(result));
	chronos_cpuset_set_pthread_affinity(result, scheduler_cpuset);
	return result;
}

// Spawn the scheduler thread
static void chronosrt_do_scheduler_thread_spawn(void *main_tcb) {
	scheduler_pthread = chronosrt_spawn_on_scheduler_core(chronosrt_scheduler_thread, main_tcb);
}

#if defined(__x86_64)
// Barrier thread routine for amd64 architecture
// NOTE: this thread ensures that no time is given to suspended processes
__attribute__((naked)) __attribute__((optimize("align-functions=4096"))) static noreturn void
chronosrt_barrier_thread_impl(void) {
	__asm__ volatile("pause\n"
	                 "jmp chronosrt_barrier_thread_impl\n"
	                 "chronosrt_barrier_thread_impl_end:");
}
extern char chronosrt_barrier_thread_impl_end[];
#else
#error "No barrier thread routine for this architecture"
#endif

// Generic barrier thread preamble + jump to the noreturn architecture-specific barrier routine
static void *chronosrt_barrier_thread_routine(void *param) {
	(void)param; // Unused
	// Disable signals
	chronosrt_disable_signals();
	// Run architecture specific barrier routine
	chronosrt_barrier_thread_impl();
}

// Spawn the barrier thread
static void chronosrt_do_barrier_thread_spawn(void) {
	// Since we want to prevent barrier thread from ever calling into kernel, we want to make sure it never page faults
	// Barrier does not access any data, so we only need to make sure that page barrier is in is always locked
	int pagesize = getpagesize();
	chronos_assert(pagesize > 0);
	uintptr_t barrier_code_start = (uintptr_t)chronosrt_barrier_thread_impl;
	uintptr_t barrier_code_end = (uintptr_t)chronosrt_barrier_thread_impl_end;
	// Align start and end to page size
	barrier_code_start = (barrier_code_start / (uintptr_t)pagesize) * (uintptr_t)pagesize;
	barrier_code_end = ((barrier_code_end + (uintptr_t)pagesize - 1) / (uintptr_t)pagesize) * (uintptr_t)pagesize;
	// Lock this memory
	chronos_assert_false(mlock((const void *)barrier_code_start, barrier_code_end - barrier_code_start));
	// Finally, we can spawn the barrier thread
	barrier_pthread = chronosrt_spawn_on_scheduler_core((void *)chronosrt_barrier_thread_routine, NULL);
}

// Set up BPF-related stuff
static void chronosrt_bpf_setup(void) {
	// Load BPF probe
	bpf_skel = trace_switch_bpf__open_and_load();
	chronos_assert(bpf_skel);
	// Set up ringbuffer and BPF maps
	ev_ringbuffer =
	    ring_buffer__new(bpf_map__fd(bpf_skel->maps.ev_ringbuffer), chronosrt_handle_trace_switch_event, NULL, NULL);
	// Attach BPF probe to the tracepoint
	chronos_assert_false(trace_switch_bpf__attach(bpf_skel));
}

void chronosrt_instantiate(struct chronosrt_config *config, void *main_tcb_backing_memory) {
	// Step 0: Copy config over and figure out which scheduling priorities to use
	cfg = *config;
	chronosrt_priorities_query();
	// Step 1: Bump real-time and mlock rlimits for this process
	struct rlimit rlp;
	rlp.rlim_cur = RLIM_INFINITY;
	rlp.rlim_max = RLIM_INFINITY;
	chronos_assert_false(setrlimit(RLIMIT_RTTIME, &rlp));
	chronos_assert_false(setrlimit(RLIMIT_MEMLOCK, &rlp));
	// Step 2: Get BPF in here
	chronosrt_bpf_setup();
	// Step 3: Figure out which CPU scheduler will run on
	size_t scheduler_cpu = 0;
	chronos_assert(chronos_cpuset_get_next_enabled(config->available_cpus, &scheduler_cpu));
	scheduler_cpuset = chronos_cpuset_with_one_cpu(scheduler_cpu);
	// Handle framework stub modes
	if (config->runtime_mode != FULL) {
		// Switch to the CPU set with all enabled CPUs. One CPU is "reserved" for scheduler
		chronos_cpuset_exclude_cpu(config->available_cpus, scheduler_cpu);
		chronos_cpuset_set_tid_affinity(0, config->available_cpus);
		if (config->runtime_mode == NOP) {
			// Update simulation start timestamp
			atomic_store(&sim_start_timestamp, chronos_ns_get_monotonic());
			return;
		} else if (config->runtime_mode == EVENTS_ONLY) {
			// Events are not implemented yet, so this mode makes no sense anyway
			chronos_assert(false);
		}
		return;
	}
	chronos_rt_log("Init: Physical core %zu will be used for the scheduler thread", scheduler_cpu);
	// Step 4: Count how many virtual CPUs can be simulated
	size_t phys_cpus_count = chronos_cpuset_count_enabled(config->available_cpus);
	chronos_assert(phys_cpus_count >= 2);
	vcpus_count = phys_cpus_count - 1;
	chronos_rt_log("Init: Simulating %zu CPUs", vcpus_count);
	// Step 5: Allocate and initialize per-vCPU data structures
	// TODO: don't use malloc
	vcpus = malloc(sizeof(struct chronosrt_vcpu) * vcpus_count);
	size_t current_physical_cpu = scheduler_cpu + 1;
	chronos_assert(vcpus);
	for (size_t i = 0; i < vcpus_count; ++i) {
		// Init per-vCPU CPU set
		chronos_assert(chronos_cpuset_get_next_enabled(config->available_cpus, &current_physical_cpu));
		size_t physical_cpu_match = current_physical_cpu++;
		vcpus[i].set = chronos_cpuset_with_one_cpu(physical_cpu_match);
		chronos_rt_log("Init: vCPU %zu will be running on physical CPU %zu", i, physical_cpu_match);
		// Init other fields
		vcpus[i].sim_time = 0;
		vcpus[i].rq = CHRONOS_PAIRING_HEAP_EMPTY;
		vcpus[i].current = NULL;
		vcpus[i].runnable_tasks = 0;
		vcpus[i].maximum_vruntime = 0;
		vcpus[i].minimum_vruntime = 0;
		vcpus[i].state = ONLINE;
	}
	// Step 6: Create and register main's thread TCB
	chronos_assert(main_tcb_backing_memory);
	struct chronosrt_tcb *main_tcb = chronosrt_init_new_tcb(main_tcb_backing_memory);
	size_t ns = chronos_ns_get_monotonic();
	chronos_vruntime_init(&main_tcb->vruntime, ns, cfg.barriers_deadline, 0);
	main_tcb->vruntime_snapshot = 0;
	main_tcb->pthread_mode = false;
	main_tcb->handle.tid = gettid();
	main_tcb->creation_vruntime = 0;
	main_tcb->yield_vruntime_diff = 0;
	chronosrt_set_tcb_ptr(main_tcb);
	// Step 7: Make main thread runnable by enqueuing it into runqueue
	chronosrt_rq_enqueue(main_tcb->vcpu, main_tcb);
	// Step 8: Spin-up scheduler thread
	chronosrt_do_scheduler_thread_spawn(main_tcb);
	// Step 9: Spin-up barrier thread
	chronosrt_do_barrier_thread_spawn();
	// Step 10: Allow scheduler to continue
	atomic_store(&scheduler_goahead, 1);
	chronosrt_fifo_bump_pthread(scheduler_pthread, scheduler_priority);
	chronosrt_fifo_bump_pthread(barrier_pthread, barrier_priority);
	// Step 11: Wait for the scheduler to give main thread a chance to run
	chronosrt_wait_for_normal(main_tcb);
}

////// API implementation ///////

// Run scheduler call
// TODO: use virtual runtime barrier to make this method appear zero-cost
static void chronosrt_do_local_call(struct chronosrt_tcb *tcb, enum chronosrt_task_state call) {
	atomic_store(&tcb->state, call);
	// Wait for the scheduler to handle local call
	while (atomic_load(&tcb->state) != NORMAL) {
		__asm__ volatile("");
	}
}

// Called from the fresh child thread. See chronos/chronosrt.h
void chronosrt_on_child_thread_hook(void *param) {
	if (cfg.runtime_mode == NOP) {
		return; // Didn't ask
	}
	chronosrt_set_tcb_ptr(param);
	chronosrt_wait_for_normal(param);
}

// Submit new thread to the scheduler with a global call
static void chronosrt_submit_new_thread(struct chronosrt_tcb *new_tcb) {
	struct chronosrt_tcb *my_tcb = chronosrt_get_tcb_ptr();
	my_tcb->thread_spawn_tcb = new_tcb;
	chronosrt_do_local_call(my_tcb, THREAD_SPAWN_PENDING);
}

// Submit new thread to the parent. See chronos/chronosrt.h
void chronosrt_on_parent_thread_hook(void *param, pid_t pid) {
	if (cfg.runtime_mode == NOP) {
		return; // I don't remember asking
	}
	struct chronosrt_tcb *tcb = param;
	tcb->pthread_mode = false;
	tcb->handle.tid = pid;
	chronosrt_submit_new_thread(tcb);
}

// See chronos/chronosrt.h
void chronosrt_on_parent_thread_hook_p(void *param, pthread_t pthread) {
	if (cfg.runtime_mode == NOP) {
		return; // We do not care
	}
	struct chronosrt_tcb *tcb = param;
	tcb->pthread_mode = true;
	tcb->handle.pthread = pthread;
	chronosrt_submit_new_thread(tcb);
}

// Set time dilation factor. See chronos/chronosrt.h
void chronosrt_set_tdf(double new_tdf) {
	if (cfg.runtime_mode == NOP) {
		return; // Still don't care
	}
	struct chronosrt_tcb *my_tcb = chronosrt_get_tcb_ptr();
	chronos_vruntime_set_tdf(&my_tcb->vruntime, new_tdf, chronos_ns_get_monotonic());
}

// Set virtual time of this thread to a given value. Returns false if value is lower than thread's current virtual time
bool chronosrt_set_virtual_time(size_t new_virtual_time) {
	if (cfg.runtime_mode == NOP) {
		return true; // Can't help
	}
	struct chronosrt_tcb *my_tcb = chronosrt_get_tcb_ptr();
	enum chronos_vruntime_update_status status = CHRONOS_VRUNTIME_RETRY_UPDATE;
	while (status == CHRONOS_VRUNTIME_RETRY_UPDATE) {
		status = chronos_vruntime_try_set(&my_tcb->vruntime, new_virtual_time, chronos_ns_get_monotonic());
	}
	return status == CHRONOS_VRUNTIME_SUCCESS;
}

// Advance virtual time of the thread by a given amount
void chronosrt_advance_virtual_time_by(size_t amount) {
	if (cfg.runtime_mode == NOP) {
		return; // Not sure if anything can be done
	}
	struct chronosrt_tcb *my_tcb = chronosrt_get_tcb_ptr();
	enum chronos_vruntime_update_status status = CHRONOS_VRUNTIME_RETRY_UPDATE;
	while (status == CHRONOS_VRUNTIME_RETRY_UPDATE) {
		status = chronos_vruntime_try_advance_by(&my_tcb->vruntime, amount, chronos_ns_get_monotonic());
	}
	// This can't happen for chronos_vruntime_advance_by
	chronos_assert(status != CHRONOS_VRUNTIME_TIMESTAMP_BEHIND);
}

// Push new barrier on the barrier stack. Old barrier is written to and can be restored from "old_tos"
void chronosrt_begin_timed_section(size_t limit, struct chronosrt_timed_section *old_tos) {
	if (cfg.runtime_mode == NOP) {
		return; // Ignored
	}
	struct chronosrt_tcb *my_tcb = chronosrt_get_tcb_ptr();
	struct chronos_vruntime_barrier old_tos_internal;
	size_t ns = chronos_ns_get_monotonic();
	chronos_vruntime_barrier_push(&my_tcb->vruntime, chronos_vruntime_get(&my_tcb->vruntime, ns) + limit,
	                              &old_tos_internal, ns);
	old_tos->barrier_real_deadline = old_tos_internal.barrier_real_deadline;
	old_tos->barrier_virtual_deadline = old_tos_internal.barrier_virtual;
}

// Restore old virtual time barrier
void chronosrt_end_timed_section(struct chronosrt_timed_section *old_tos) {
	if (cfg.runtime_mode == NOP) {
		return; // Ignored
	}
	struct chronosrt_tcb *my_tcb = chronosrt_get_tcb_ptr();
	struct chronos_vruntime_barrier old_tos_internal;
	old_tos_internal.barrier_real_deadline = old_tos->barrier_real_deadline;
	old_tos_internal.barrier_virtual = old_tos->barrier_virtual_deadline;
	size_t new_vtime = chronos_vruntime_barrier_pop(&my_tcb->vruntime, &old_tos_internal);
	chronosrt_set_virtual_time(new_vtime);
}

// Get thread's runtime. See chronos/chronosrt.h
size_t chronosrt_get_thread_running_time(void) {
	if (cfg.runtime_mode == NOP) {
		// Linux knows better
		return chronos_ns_get_clock(CLOCK_THREAD_CPUTIME_ID);
	}
	struct chronosrt_tcb *my_tcb = chronosrt_get_tcb_ptr();
	return chronos_vruntime_get(&my_tcb->vruntime, chronos_ns_get_monotonic()) - my_tcb->creation_vruntime;
}

// Get simulation's runtime. See chronos/chronosrt.h
size_t chronosrt_get_sim_time(void) {
	if (cfg.runtime_mode == NOP) {
		return chronos_ns_get_monotonic() - sim_start_timestamp;
	}
	return atomic_load(&gvt);
}

// To be called on thread exit. See chronos/chronosrt.h
void *chronosrt_on_exit_thread(void) {
	if (cfg.runtime_mode == NOP) {
		return NULL; // Who asked?
	}
	// Notify scheduler this task will exit soon
	struct chronosrt_tcb *my_tcb = chronosrt_get_tcb_ptr();
	chronosrt_do_local_call(my_tcb, EXIT_THREAD_PENDING);
	// We can now guarantee that current task won't be preempted
	// Hence this task is not going to migrate between CPUs and ID(my_tcb->vcpu) is now constant
	chronos_vcpu_index_t val = ID(my_tcb->vcpu);
	// Tell BPF probe which CPU is exiting task is running on
	pid_t key = gettid();
	chronos_assert_false(
	    bpf_map__update_elem(bpf_skel->maps.on_exit_mapped_to_vcpu, &key, sizeof(key), &val, sizeof(val), BPF_NOEXIST));
	return my_tcb;
}

// Replacement for sched_yield(). See chronos/chronosrt.h
void chronosrt_yield(void) {
	if (cfg.runtime_mode == NOP) {
		sched_yield(); // Linux scheduler will do it for us
		return;
	}
	struct chronosrt_tcb *my_tcb = chronosrt_get_tcb_ptr();
	chronosrt_do_local_call(my_tcb, YIELD_PENDING);
}

// Report average scheduler loop duration. See chronos/chronosrt.h
size_t chronosrt_calc_avg_loop_duration(void) {
	if (cfg.runtime_mode == NOP) {
		return 0; // There is no scheduler loop
	}
	// Loops is the only thing scheduler is doing, so if divide simulation time by the number of loops, we get average
	// loop duration.
	size_t emulation_ran_for = chronos_ns_get_monotonic() - atomic_load(&sim_start_timestamp);
	return emulation_ran_for / atomic_load(&loops_done);
}

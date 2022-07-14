#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <assert.h>
#include <chronos.h>
#include <errno.h>
#include <pairing_heap.h>
#include <pthread.h>
#include <sched.h>
#include <semaphore.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

// TDF granularity. TDF is then stored as (size_t)(double_tdf * (double)TDF_UNIT)
#define TDF_UNIT 1000000

// Max permitted vruntime difference (/ vruntime_step) for threads running within the same round. Prevents threads from
// getting to far ahead in virtual time and still being scheduled.
#define MAX_VRUNTIME_DIFF_FACTOR 2.0

// Chronos runtime instance.
struct chronos_rt {
	// Lock to ensure mutual exclusion when accessing thread queue
	pthread_mutex_t lock;
	// A semaphore scheduler thread waits on. Initially its used for bootstrap (scheduler thread waits for one token,
	// allowing main thread to finish initialization and submit itself to the scheduler). Later it is used by threads
	// running on the scheduler to signal completition of their timeslices.
	sem_t notify_sched_sem;
	// CPU scheduler thread has been assigned to
	size_t scheduler_cpu_id;
	// CPU set for chronos-managed threads
	cpu_set_t *mask;
	size_t cpusetsize;
	size_t cpu_alloc_size;
	// Scratch CPU mask
	cpu_set_t *scratch_mask;
	// Pairing heap for scheduled threads
	pairing_heap_t heap;
	// vruntime step target
	size_t vruntime_step;
};

// Chronos thread-local state.
struct chronos_thread_state {
	// Pairing heap node
	pairing_heap_node_t node;
	// thread ID
	size_t tid;
	// Semaphore used by the scheduler to pause threads
	sem_t sched_go_ahead_sem;
	// Pointer to the runtime
	struct chronos_rt *rt;
	// Last vruntime
	size_t vruntime;
	// Average TDF for the last round
	size_t average_tdf;
	// Round start time in ns
	size_t round_start_ns;
	// vruntime on round start
	size_t round_start_vruntime;
	// Last time vruntime value has been updated
	size_t last_vruntime_calc_ns;
	// Current TDF as a percentage
	size_t tdf;
	// Deadline (as specified by the scheduler thread)
	size_t relative_timer_deadline;
	// Target vruntime at round completition
	size_t completition_vruntime;
	// Thread's expiration timer
	timer_t timer;
};

static __thread struct chronos_thread_state *chronos_tcb = NULL;

#define NS_IN_S 1000000000

// Signal vector used by chronos for timer interrupts
#define TIMER_VECTOR SIGALRM

// Convert timespec into nanoseconds
static size_t chronos_timespec_to_nsec(struct timespec const *timespec) {
	return timespec->tv_sec * NS_IN_S + timespec->tv_nsec;
}

// Convert nanoseconds into timespec
static size_t chronos_nsec_to_timespec(struct timespec *timespec, size_t nsec) {
	timespec->tv_sec = nsec / NS_IN_S;
	timespec->tv_nsec = nsec % NS_IN_S;
}

// Get nanoseconds since epoch
static size_t chronos_nsecs_since_epoch() {
	struct timespec tp;
	if (clock_gettime(CLOCK_MONOTONIC, &tp)) {
		dprintf(2, "libchronos: clock_gettime(CLOCK_MONOTONIC) failed\n");
		abort();
	}
	return chronos_timespec_to_nsec(&tp);
}

// Schedule thread to run on a given CPU
static void chronos_send_tid_to_cpu(size_t tid, size_t cpu, cpu_set_t *scratch_mask, size_t cpusetsize,
                                    size_t cpu_alloc_size) {
	// Set thread affinity
	CPU_ZERO_S(cpu_alloc_size, scratch_mask);
	CPU_SET_S(cpu, cpu_alloc_size, scratch_mask);
	sched_setaffinity(tid, cpusetsize, scratch_mask);
	// Set thread priority
	struct sched_param param;
	param.sched_priority = sched_get_priority_max(SCHED_FIFO);
	sched_setscheduler(tid, SCHED_FIFO, &param);
}

// Runtime cleanup routine
static void chronos_rt_cleanup(struct chronos_rt *rt) {
	CPU_FREE(rt->mask);
	CPU_FREE(rt->scratch_mask);
	pthread_mutex_destroy(&rt->lock);
	sem_destroy(&rt->notify_sched_sem);
	free(rt);
}

// Scheduler thread routine
static void *chronos_rt_scheduler(void *arg) {
	struct chronos_rt *rt = arg;
	// Claim scheduler CPU
	chronos_send_tid_to_cpu(0, rt->scheduler_cpu_id, rt->scratch_mask, rt->cpusetsize, rt->cpu_alloc_size);
	// Wait for the init thread to finish initialization
	sem_wait(&rt->notify_sched_sem);
	// Runtime bootstrap done, enter main loop
	while (true) {
		// prepare for the new round, lock the runtime instance
		pthread_mutex_lock(&rt->lock);
		// if runnable thread queue is empty, drop instance
		if (!pairing_heap_get_min(&rt->heap)) {
			pthread_mutex_unlock(&rt->lock);
			break;
		}
		size_t runnable_threads = 0;
		// pick threads for each CPU
		bool some_thread_picked = false;
		size_t vruntime_threshold = 0;
		for (size_t i = 0; i < rt->cpusetsize; ++i) {
			if (CPU_ISSET_S(i, rt->cpu_alloc_size, rt->mask)) {
				// pick thread with lowest vruntime from the queue
				pairing_heap_node_t *node = pairing_heap_get_min(&rt->heap);
				if (node == NULL) {
					break;
				}
				struct chronos_thread_state *tcb = (struct chronos_thread_state *)node;
				if (!some_thread_picked) {
					some_thread_picked = true;
					vruntime_threshold = tcb->vruntime + (size_t)((double)rt->vruntime_step * MAX_VRUNTIME_DIFF_FACTOR);
				}
				// check that thread vruntime is within appropriate bounds
				if (tcb->vruntime > vruntime_threshold) {
					break;
				}
				// delete thread from the runqueue
				pairing_heap_del_min(&rt->heap);
				// calculate how long thread should run for
				tcb->relative_timer_deadline = (rt->vruntime_step * tcb->average_tdf) / TDF_UNIT;
				// start the thread
				chronos_send_tid_to_cpu(tcb->tid, rt->scheduler_cpu_id, rt->scratch_mask, rt->cpusetsize,
				                        rt->cpu_alloc_size);
				sem_post(&tcb->sched_go_ahead_sem);
				runnable_threads++;
			}
		}
		// unlock the runtime instance
		pthread_mutex_unlock(&rt->lock);
		// wait for the round to finish
		for (size_t i = 0; i < runnable_threads; ++i) {
			sem_wait(&rt->notify_sched_sem);
		}
	}
	// Cleanup current instance
	chronos_rt_cleanup(rt);
	return NULL;
}

// Set timer to fire at a given nanosecond count
static void chronos_schedule_timer_in(size_t nsec) {
	struct itimerspec its;
	chronos_nsec_to_timespec(&its.it_value, nsec);
	its.it_interval.tv_nsec = 0;
	its.it_interval.tv_sec = 0;
	if (timer_settime(chronos_tcb->timer, 0, &its, NULL)) {
		dprintf(2, "libchronos: timer_settime failed\n");
		abort();
	}
}

// Disarm timer
static void chronos_disarm_timer() {
	struct itimerspec its;
	its.it_value.tv_nsec = 0;
	its.it_value.tv_sec = 0;
	its.it_interval.tv_nsec = 0;
	its.it_interval.tv_sec = 0;
	if (timer_settime(chronos_tcb->timer, 0, &its, NULL)) {
		dprintf(2, "libchronos: timer_settime failed\n");
		abort();
	}
}

// Recalculate vruntime field
void chronos_update_runtimes() {
	// Calculate delta in nanoseconds
	size_t old_nsec = chronos_tcb->last_vruntime_calc_ns;
	size_t new_nsec = chronos_nsecs_since_epoch();
	size_t delta = new_nsec - old_nsec;
	// Update vruntime
	size_t scaled_delta = (delta * TDF_UNIT) / chronos_tcb->tdf;
	chronos_tcb->vruntime += scaled_delta;
	chronos_tcb->last_vruntime_calc_ns = new_nsec;
}

// Enqueue current thread into runnable queue
static void chronos_enqueue_runnable() {
	struct chronos_rt *rt = chronos_tcb->rt;
	pairing_heap_node_t *node = &chronos_tcb->node;
	pthread_mutex_lock(&rt->lock);
	node->key = chronos_tcb->vruntime;
	pairing_heap_insert(&rt->heap, node);
	pthread_mutex_unlock(&rt->lock);
}

// Wait for the scheduler to signal that our thread is now runnable
static void chronos_wait_from_ack_from_sched() {
	sem_wait(&chronos_tcb->sched_go_ahead_sem);
	// Start of a new timeslice.
	chronos_schedule_timer_in(chronos_tcb->relative_timer_deadline);
	chronos_tcb->round_start_ns = chronos_nsecs_since_epoch();
	chronos_tcb->last_vruntime_calc_ns = chronos_tcb->round_start_ns;
	chronos_tcb->round_start_vruntime = chronos_tcb->vruntime;
}

// Tell scheduler this thread is done with a timeslice
static void chronos_signal_this_timeslice_done() {
	sem_post(&chronos_tcb->rt->notify_sched_sem);
}

// Preemption handler
static void chronos_on_round_end() {
	chronos_update_runtimes();
	// Calculate average TDF for the scheduler
	size_t ns_delta = chronos_nsecs_since_epoch() - chronos_tcb->round_start_ns;
	size_t vruntime_delta = chronos_tcb->vruntime - chronos_tcb->round_start_vruntime;
	chronos_tcb->average_tdf = (TDF_UNIT * ns_delta) / vruntime_delta;
	// Enqueue current thread
	chronos_enqueue_runnable();
	// Tell scheduler we have finished our timeslice
	chronos_signal_this_timeslice_done();
	// Wait till scheduler gives us another timeslice
	chronos_wait_from_ack_from_sched();
}

static __thread volatile sig_atomic_t chronos_disable_preemption = 0;
static __thread volatile sig_atomic_t chronos_preemption_pending = 0;

// Signal preemtion
static void chronos_signal_preemption() {
	if (chronos_preemption_pending) {
		return;
	} else if (chronos_disable_preemption) {
		chronos_preemption_pending = 1;
	} else {
		chronos_on_round_end();
	}
}

// Enter critical section (disable rescheduling). Returns critical section id that is to be passed to
// chronos_exit_critical
int chronos_enter_critical() {
	if (chronos_disable_preemption) {
		return 1;
	}
	chronos_disable_preemption = 1;
	return 0;
}

// Exit critical section (enable rescheduling). Accepts critical section id from chronos_enter_critical
static void chronos_exit_critical_impl(int section_id) {
	assert(chronos_disable_preemption);
	if (section_id == 1) {
		return;
	}
	chronos_disable_preemption = 0;
	if (chronos_preemption_pending) {
		// Timer interrupt arrived while we were in critical section
		// Run deferred preemption routine now
		chronos_preemption_pending = 0;
		chronos_on_round_end();
	}
}

// Do vruntime check and see if its time to preempt. Run with preemption disabled
static void chronos_vruntime_check_impl() {
	assert(chronos_disable_preemption);
	if (chronos_tcb->vruntime > chronos_tcb->completition_vruntime) {
		// stop timer. We don't have to per se, but why not
		chronos_disarm_timer();
		// this will have effect as soon as we exit the outermost critical section
		chronos_signal_preemption();
	}
}

// Public exit critical section function. Checks vruntime
void chronos_exit_critical(int section_id) {
	chronos_update_runtimes();
	chronos_vruntime_check_impl();
	chronos_exit_critical_impl(section_id);
}

// Check that vruntime is wtihin bounds and preempt if necessary. Won't have effect until outermost critical section is
// exited.
void chronos_vruntime_check() {
	if (chronos_disable_preemption) {
		// chronos_exit_criticial will do the check
		return;
	}
	// Vruntime check requires that we run inside a critical section
	// chronos_exit_critical() does a vruntime check on its own
	// Hence entering critical section and exitiing it immediatelly does the trick
	chronos_exit_critical(chronos_enter_critical());
}

// Set up timer
static void chronos_set_up_timer() {
	// Set up SIGALRM singal handler
	sigset_t mask;
	struct sigaction sa;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = 0;
	sa.sa_handler = &chronos_signal_preemption;
	if (sigaction(TIMER_VECTOR, &sa, NULL)) {
		dprintf(2, "libchronos: sigaction failed\n");
		abort();
	}
	// Set up timer
	struct sigevent sev;
	sev.sigev_notify = SIGEV_THREAD_ID;
	sev.sigev_signo = TIMER_VECTOR;
	sev._sigev_un._tid = gettid();
	if (timer_create(CLOCK_MONOTONIC, &sev, &chronos_tcb->timer)) {
		dprintf(2, "libchronos: timer_create failed: %s\n", strerror(errno));
		abort();
	}
}

static void chronos_rt_submit_nowait(void *rt_token, size_t initial_vruntime) {
	struct chronos_rt *rt = rt_token;
	// Allocate chronos TCB
	struct chronos_thread_state *state = malloc(sizeof(struct chronos_thread_state));
	if (state == NULL) {
		dprintf(2, "libchronos: malloc failed\n");
		abort();
	}
	chronos_tcb = state;
	if (sem_init(&chronos_tcb->sched_go_ahead_sem, 0, 0)) {
		dprintf(2, "libchronos: sem_init failed\n");
		abort();
	}
	chronos_tcb->tid = gettid();
	chronos_tcb->rt = rt;
	chronos_tcb->vruntime = initial_vruntime;
	chronos_tcb->tdf = TDF_UNIT;
	chronos_tcb->average_tdf = TDF_UNIT;
	chronos_set_up_timer();
	// Tell scheduler we are now runnable
	chronos_enqueue_runnable();
}

void *chronos_rt_init(size_t cpusetsize, cpu_set_t const *mask, size_t vruntime_step) {
	// Allocate memory for chronos runtime
	struct chronos_rt *rt = malloc(sizeof(struct chronos_rt));
	if (rt == NULL) {
		return NULL;
	}
	rt->vruntime_step = vruntime_step;
	// Initialize runtime mutex
	if (pthread_mutex_init(&rt->lock, NULL)) {
		goto free_rt;
	}
	// Initialize scheduler thread notification semaphore
	if (sem_init(&rt->notify_sched_sem, 0, 0)) {
		goto destroy_mutex;
	}
	// Go over CPUs and allocate one CPU to the scheduler thread
	// Copy CPU mask in the process
	rt->mask = CPU_ALLOC(cpusetsize);
	rt->scratch_mask = CPU_ALLOC(cpusetsize);
	if (rt->mask == NULL || rt->scratch_mask == NULL) {
		goto destroy_sem;
	}
	rt->cpusetsize = cpusetsize;
	rt->cpu_alloc_size = CPU_ALLOC_SIZE(cpusetsize);
	CPU_ZERO_S(rt->cpu_alloc_size, rt->mask);
	bool scheduler_got_a_cpu = false;
	for (size_t i = 0; i < cpusetsize; ++i) {
		if (!CPU_ISSET_S(i, rt->cpu_alloc_size, mask)) {
			continue;
		} else if (!scheduler_got_a_cpu) {
			// Allocate CPU i to the scheduler
			rt->scheduler_cpu_id = i;
			scheduler_got_a_cpu = true;
		} else {
			CPU_SET_S(i, rt->cpu_alloc_size, rt->mask);
		}
	}
	if (!scheduler_got_a_cpu) {
		goto free_mask;
	}
	// Start scheduler thread
	pthread_t scheduler_thread;
	if (pthread_create(&scheduler_thread, NULL, chronos_rt_scheduler, rt)) {
		goto free_mask;
	}
	if (pthread_detach(scheduler_thread)) {
		goto free_mask;
	}
	// Submit to the scheduler
	chronos_rt_submit_nowait(rt, 0);
	// Signal the scheduler that we are now runnable
	sem_post(&rt->notify_sched_sem);
	// Wait until scheduler gives a timeslice
	chronos_wait_from_ack_from_sched();
	return rt;
free_mask:
	if (rt->mask != NULL) {
		CPU_FREE(rt->mask);
	}
	if (rt->scratch_mask != NULL) {
		CPU_FREE(rt->mask);
	}
destroy_mutex:
	pthread_mutex_destroy(&rt->lock);
destroy_sem:
	sem_destroy(&rt->notify_sched_sem);
free_rt:
	free(rt);
	return NULL;
}

void chronos_rt_submit(void *rt_token, size_t initial_vruntime) {
	chronos_rt_submit_nowait(rt_token, initial_vruntime);
	// Wait until scheduler gives us another timeslice
	chronos_wait_from_ack_from_sched();
}

void chronos_rt_detatch() {
	// Delete timer, we won't need it anymore
	timer_delete(chronos_tcb->timer);
	// TODO: switch to a different CPU
	// Tell scheduler we have finished running this timeslice. We have not enqueued this thread into runqueue, so
	// this thread is not coming back
	chronos_signal_this_timeslice_done();
	// And we are done... Scheduler thread does not know we exist anymore
	sem_destroy(&chronos_tcb->sched_go_ahead_sem);
	free(chronos_tcb);
	chronos_tcb = NULL;
}

void chronos_set_tdf(double tdf) {
	chronos_vruntime_check();
	size_t tdf_as_int = (size_t)((double)TDF_UNIT * tdf);
	chronos_update_runtimes();
	chronos_tcb->tdf = tdf_as_int;
}

void chronos_jump(size_t delta_ns) {
	chronos_tcb->vruntime += delta_ns;
	chronos_vruntime_check();
}

size_t chronos_get_vruntime() {
	chronos_vruntime_check();
	chronos_update_runtimes();
	return chronos_tcb->vruntime;
}

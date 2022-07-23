#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <asm/prctl.h>
#include <linux/bpf.h>
#include <pthread.h>
#include <sched.h>
#include <semaphore.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>

#include <chronosrt/chronosrt.h>
#include <chronosrt/cpuset.h>
#include <fail.h>
#include <log.h>
#include <nosignals.h>
#include <ns.h>
#include <pairing_heap.h>
#include <runner.h>
#include <schedpolicy.h>
#include <spinlock.h>
#include <vruntime.h>

struct chronosrt_tcb {
	struct chronosrt_vruntime vruntime_info;
	pairing_heap_node_t queue_node;
	struct chronosrt_tcb *self;
	struct chronosrt_tcb *next_new_thread;
	struct chronosrt_cpu *running_on_cpu;
	pid_t tid;
	bool first_time;
};

// Can be assigned from within the critical section to tell per-cpu scheduler what to do once the timeslice expires
enum chronosrt_timeslice_status {
	CHRONOSRT_AGAIN = 0,
	CHRONOSRT_DETACH = 1,
};

struct chronosrt_cpu {
	struct chronosrt_runner runner;
	pthread_t scheduler_handle;
	sem_t semaphore;
	struct chronosrt_tcb *task_to_run;
	size_t sleep_for;
	struct chronosrt_tcb *_Atomic new_threads_list;
	_Atomic enum chronosrt_timeslice_status timeslice_status;
};

struct chronosrt {
	struct chronosrt_cfg cfg;
	struct chronosrt_cpu *cpus;
	size_t scheduler_cpu;
	size_t cpus_count;
	pairing_heap_t runqueue;
	chronosrt_spinlock_t runqueue_lock;
	sem_t scheduler_sem;
} chronosrt;

static void chronosrt_set_tcb_ptr(struct chronosrt_tcb *tcb) {
#if defined(__x86_64)
	CHRONOSRT_ASSERT_FALSE(syscall(SYS_arch_prctl, ARCH_SET_GS, tcb));
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

void chronosrt_enter_critical(void) {
	chronosrt_runner_disable_preemption(0);
}

void chronosrt_exit_critical(void) {
	chronosrt_runner_enable_preemption(0);
}

static struct chronosrt_tcb *chronosrt_new_thread_token_impl(size_t vruntime, struct chronosrt_cpu *cpu) {
	struct chronosrt_tcb *tcb = malloc(sizeof(struct chronosrt_tcb));
	CHRONOSRT_ASSERT_TRUE(tcb);
	tcb->first_time = true;
	tcb->running_on_cpu = cpu;
	chronosrt_vruntime_set_virtual_ts(&tcb->vruntime_info, vruntime);
	tcb->vruntime_info.tdf = CHRONOSRT_VRUNTIME_TDF_UNIT;
	return tcb;
}

void *chronosrt_new_thread_token() {
	struct chronosrt_tcb *my_tcb = chronosrt_get_tcb_ptr();
	size_t spawn_vruntime = chronosrt_vruntime_get(&my_tcb->vruntime_info);
	return chronosrt_new_thread_token_impl(spawn_vruntime, my_tcb->running_on_cpu);
}

static void chronosrt_complete_tcb_init(struct chronosrt_tcb *tcb) {
	tcb->tid = gettid();
	chronosrt_set_tcb_ptr(tcb);
}

void chronosrt_attach(void *token) {
	struct chronosrt_tcb *my_tcb = token;
	chronosrt_complete_tcb_init(my_tcb);

	chronosrt_runner_disable_preemption(0);
	struct chronosrt_cpu *cpu = my_tcb->running_on_cpu;

	// Enqueue new task into new threads queue
	my_tcb->next_new_thread = atomic_load_explicit(&cpu->new_threads_list, memory_order_relaxed);
	atomic_store_explicit(&cpu->new_threads_list, my_tcb, memory_order_relaxed);
	chronosrt_runner_freeze(0); // Scheduling equivalent of suicide
}

void chronosrt_detach(void) {
	chronosrt_runner_disable_preemption(0);
	// Tell per-cpu scheduler that we shouldn't be enqueued again
	struct chronosrt_tcb *my_tcb = chronosrt_get_tcb_ptr();
	CHRONOSRT_LOG("thread %p called chronosrt_detach()", my_tcb);
	atomic_store_explicit(&my_tcb->running_on_cpu->timeslice_status, CHRONOSRT_DETACH, memory_order_relaxed);
	chronosrt_runner_enable_preemption(0);
}

static void chronosrt_rq_insert(struct chronosrt_tcb *tcb) {
	tcb->queue_node.key = chronosrt_vruntime_get(&tcb->vruntime_info);
	pairing_heap_insert(&chronosrt.runqueue, &tcb->queue_node);
}

static struct chronosrt_tcb *chronosrt_rq_get_min() {
	struct pairing_heap_node *res = pairing_heap_get_min(&chronosrt.runqueue);
	if (res == NULL) {
		return NULL;
	}
	return (struct chronosrt_tcb *)((uintptr_t)res - offsetof(struct chronosrt_tcb, queue_node));
}

static struct chronosrt_tcb *chronosrt_rq_del_min() {
	struct pairing_heap_node *res = pairing_heap_del_min(&chronosrt.runqueue);
	if (res == NULL) {
		return NULL;
	}
	return (struct chronosrt_tcb *)((uintptr_t)res - offsetof(struct chronosrt_tcb, queue_node));
}

// A really dumb scheduling algorithm
// TODO: rewrite this, this is just awful
static size_t chronosrt_map_tasks(struct chronosrt_cpu *sched_cpu) {
	(void)sched_cpu;
	size_t waiting_for = 0;
	bool picked_one_task = false;
	size_t min_vruntime = 0;

	for (size_t i = 0; i < chronosrt.cpus_count; ++i) {
		struct chronosrt_tcb *next = chronosrt_rq_get_min();

		if (picked_one_task && next->queue_node.key - min_vruntime > chronosrt.cfg.max_vruntime_diff) {
			break;
		} else if (!picked_one_task) {
			picked_one_task = true;
			min_vruntime = next->queue_node.key;
		}
		chronosrt_rq_del_min();

		waiting_for++;
		chronosrt.cpus[i].task_to_run = next;
		chronosrt.cpus[i].sleep_for = chronosrt.cfg.ns_min_timeslice;

		CHRONOSRT_LOG("scheduler: assigning task %p to CPU %zu (vruntime: %zu, real time timeslice length: %zu).", next,
		              i, next->queue_node.key, chronosrt.cpus[i].sleep_for);
		sem_post(&chronosrt.cpus[i].semaphore);
	}

	return waiting_for;
}

static void *chronosrt_scheduler_for_cpu(void *cpu_arg) {
	struct chronosrt_cpu *cpu = cpu_arg;

	// Prevent signals from being handled on this thread
	chronosrt_disable_signals();

	bool scheduler = chronosrt.cpus + 0 == cpu;

	// Number of cores scheduler is waiting on to finish timeslices
	// NOTE: we set this to 1 on startup so that init thread can signal the scheduler its done with initialization
	size_t waiting_for = 1;

	for (;;) {
		if (scheduler) {
			// Wait for the last round to finish
			CHRONOSRT_LOG("scheduler: waiting for the round to finish...");
			for (size_t i = 0; i < waiting_for; ++i) {
				sem_wait(&chronosrt.scheduler_sem);
			}
			// Map tasks to CPUs
			CHRONOSRT_LOG("scheduler: round done, mapping tasks to CPU...");
			waiting_for = chronosrt_map_tasks(cpu);
		}

		// Wait for the scheduler to assign us a task
		sem_wait(&cpu->semaphore);

		CHRONOSRT_LOG("CPU %zu got a task to run (%p)", cpu - chronosrt.cpus, cpu->task_to_run);

		// Let the task run
		chronosrt_vruntime_set_real_ts(&cpu->task_to_run->vruntime_info, chronosrt_ns_from_boot());
		atomic_store_explicit(&cpu->timeslice_status, CHRONOSRT_AGAIN, memory_order_relaxed);
		chronosrt_runner_make_runnable_on(cpu->task_to_run->tid, &cpu->runner);

		// Wait for the expiration of the timeslice
		CHRONOSRT_LOG("CPU %zu about to enter nanosleep for %zu nanoseconds", cpu - chronosrt.cpus, cpu->sleep_for);
		chronosrt_sleep(cpu->sleep_for);

		// Check if thread has detached
		enum chronosrt_timeslice_status status = atomic_load_explicit(&cpu->timeslice_status, memory_order_relaxed);
		if (status == CHRONOSRT_AGAIN) {
			cpu->task_to_run->first_time = false;
			// Pause the thread
			chronosrt_runner_freeze(cpu->task_to_run->tid);
			// Update thread's vruntime
			chronosrt_vruntime_update(&cpu->task_to_run->vruntime_info);
			CHRONOSRT_LOG("Task on CPU %zu completed timeslice with CHRONOSRT_AGAIN status", cpu - chronosrt.cpus);
		} else {
			CHRONOSRT_LOG("Task on CPU %zu completed timeslice with CHRONOSRT_DETACH status", cpu - chronosrt.cpus);
		}

		{
			size_t ticket = chronosrt_spinlock(chronosrt.runqueue_lock);
			CHRONOSRT_LOG("CPU %zu acquired runqueue lock", cpu - chronosrt.cpus);

			// Enqueue new threads
			struct chronosrt_tcb *head = cpu->new_threads_list;
			while (head != NULL) {
				chronosrt_rq_insert(head);
				head = head->next_new_thread;
			}
			cpu->new_threads_list = NULL;

			// Enqueue current task
			if (status == CHRONOSRT_AGAIN) {
				chronosrt_rq_insert(cpu->task_to_run);
			}

			CHRONOSRT_LOG("CPU %zu is about to release runqueue lock", cpu - chronosrt.cpus);
			chronosrt_spinunlock(chronosrt.runqueue_lock, ticket);
		}

		// Tell the scheduler this timeslice is done
		sem_post(&chronosrt.scheduler_sem);
	}
	return NULL;
}

void chronosrt_instantiate_runtime(struct chronosrt_cfg *cfg) {
	// Copy config over and init nofication semaphore
	chronosrt.cfg = *cfg;
	chronosrt_sched_policy_init(cfg);
	CHRONOSRT_ASSERT_FALSE(sem_init(&chronosrt.scheduler_sem, 0, 0));

	// Pick CPU to run barrier and scheduler on
	chronosrt.scheduler_cpu = chronosrt_cpuset_first_set(cfg->cpuset);
	if (chronosrt.scheduler_cpu == chronosrt_cpuset_size(cfg->cpuset)) {
		// No CPU set
		chronosrt_fail("No CPU specified in the bitmask");
	}
	// Spawn suspension barrier
	chronosrt_runner_freezer_init(chronosrt.scheduler_cpu);

	// Allocate per-cpu storage
	size_t cpus_count = chronosrt_cpuset_count(cfg->cpuset);
	chronosrt.cpus = malloc(sizeof(struct chronosrt_cpu) * cpus_count);
	chronosrt.cpus_count = cpus_count;
	CHRONOSRT_ASSERT_TRUE(chronosrt.cpus);

	chronosrt.runqueue_lock = chronosrt_new_spinlock(cpus_count);

	// Scheduler threads we spawn can preempt us otherwise and we wouldn't get a chance to run
	chronosrt_runner_disable_preemption(0);

	// Spawn per-cpu scheduler threads
	size_t cpusetsize = chronosrt_cpuset_size(cfg->cpuset);
	size_t cpus_idx = 0;
	for (size_t i = 0; i < cpusetsize; ++i) {
		if (!chronosrt_cpuset_get(cfg->cpuset, i)) {
			continue;
		}
		struct chronosrt_cpu *cpu = chronosrt.cpus + cpus_idx++;
		CHRONOSRT_ASSERT_FALSE(sem_init(&cpu->semaphore, 0, 0));

		// Spawn a scheduler thread on this CPU
		chronosrt_runner_init(&cpu->runner, i);
		CHRONOSRT_ASSERT_FALSE(pthread_create(&cpu->scheduler_handle, NULL, chronosrt_scheduler_for_cpu, cpu));
		CHRONOSRT_ASSERT_FALSE(pthread_detach(cpu->scheduler_handle));
		chronosrt_runner_make_scheduler_on_p(cpu->scheduler_handle, &cpu->runner);
	}

	// Switch to the CPU global scheduler runs on
	chronosrt_runner_make_scheduler_on(0, &chronosrt.cpus[0].runner);

	// Submit this thread to the scheduler
	struct chronosrt_tcb *token = chronosrt_new_thread_token_impl(0, chronosrt.cpus + 0);
	chronosrt_complete_tcb_init(token);
	chronosrt_rq_insert(token);
	sem_post(&chronosrt.scheduler_sem);
	CHRONOSRT_LOG("init thread about to enter sched_yield()");
	sched_yield(); // yield to the scheduler that will degrade us to runnable priority
	CHRONOSRT_LOG("init thread exited sched_yield()");
}

void chronosrt_set_tdf(double new_tdf) {
	struct chronosrt_tcb *tcb = chronosrt_get_tcb_ptr();
	chronosrt_vruntime_update(&tcb->vruntime_info);
	chronosrt_vruntime_set_tdf(&tcb->vruntime_info, new_tdf);
}

size_t chronosrt_get_virtual_time(void) {
	struct chronosrt_tcb *tcb = chronosrt_get_tcb_ptr();
	chronosrt_vruntime_update(&tcb->vruntime_info);
	return chronosrt_vruntime_get(&tcb->vruntime_info);
}

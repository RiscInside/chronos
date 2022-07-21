#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <pthread.h>
#include <sched.h>

#include <affinity.h>
#include <fail.h>
#include <nosignals.h>
#include <runner.h>
#include <schedpolicy.h>

static void *chronostrt_runner_freeeze_barrier(void *arg) {
	(void)arg;

	// Ensure that this thread does not handle any signals. We really don't want this thread to get suspended while
	// handling some signal with a user-defined signal handler
	chronosrt_disable_signals();

	for (;;) {
#ifdef __x86_64
		// TODO: use umwait if available
		asm volatile("pause");
#endif
	}
	return NULL;
}

static chronosrt_cpuset_t *freezer_cpuset;

void chronosrt_runner_freezer_init(size_t freezer_cpu) {
	pthread_t freezer_handle;
	CHRONOSRT_ASSERT_FALSE(pthread_create(&freezer_handle, NULL, chronostrt_runner_freeeze_barrier, NULL));
	CHRONOSRT_ASSERT_FALSE(pthread_detach(freezer_handle));

	freezer_cpuset = chronosrt_cpuset_create_with_one_set(freezer_cpu);
	chronosrt_set_affinity_p(freezer_handle, freezer_cpuset);
	chronosrt_sched_policy_set_barrier_p(freezer_handle);
}

void chronosrt_runner_freeze(pid_t tid) {
	chronosrt_sched_policy_set_suspended(tid);
	chronosrt_set_affinity(tid, freezer_cpuset);
}

void chronosrt_runner_freeze_p(pthread_t pthread) {
	chronosrt_sched_policy_set_suspended_p(pthread);
	chronosrt_set_affinity_p(pthread, freezer_cpuset);
}

void chronosrt_runner_init(struct chronosrt_runner *runner, size_t cpu) {
	runner->cpuset = chronosrt_cpuset_create_with_one_set(cpu);
}

void chronosrt_runner_schedule_on(pid_t tid, struct chronosrt_runner *runner) {
	chronosrt_set_affinity(tid, runner->cpuset);
	chronosrt_sched_policy_set_runnable(tid);
}

void chronosrt_runner_schedule_on_p(pthread_t thread, struct chronosrt_runner *runner) {
	chronosrt_set_affinity_p(thread, runner->cpuset);
	chronosrt_sched_policy_set_runnable_p(thread);
}

void chronosrt_runner_disable_preemption(pid_t tid) {
	chronosrt_sched_policy_set_scheduler(tid);
}

void chronosrt_runner_disable_preemption_p(pthread_t thread) {
	chronosrt_sched_policy_set_scheduler_p(thread);
}

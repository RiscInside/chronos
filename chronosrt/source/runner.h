#ifndef __CHRONOSRT_RUNNER_H__
#define __CHRONOSRT_RUNNER_H__

#include <chronosrt/cpuset.h>
#include <sched.h>
#include <stddef.h>
#include <sys/types.h>

struct chronosrt_runner {
	chronosrt_cpuset_t *cpuset;
};

void chronosrt_runner_freezer_init(size_t freezer_cpu);

void chronosrt_runner_freeze(pid_t tid);

void chronosrt_runner_freeze_p(pthread_t pthread);

void chronosrt_runner_init(struct chronosrt_runner *runner, size_t cpu);

void chronosrt_runner_disable_preemption(pid_t tid);

void chronosrt_runner_disable_preemption_p(pthread_t pthread);

void chronosrt_runner_enable_preemption(pid_t tid);

void chronosrt_runner_enable_preemption_p(pthread_t pthread);

void chronosrt_runner_make_runnable_on(pid_t tid, struct chronosrt_runner *runner);

void chronosrt_runner_make_runnable_on_p(pthread_t pthread, struct chronosrt_runner *runner);

void chronosrt_runner_make_scheduler_on(pid_t tid, struct chronosrt_runner *runner);

void chronosrt_runner_make_scheduler_on_p(pthread_t pthread, struct chronosrt_runner *runner);

#endif

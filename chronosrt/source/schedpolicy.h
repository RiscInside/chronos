#ifndef __CHRONOSRT_SCHED_POLICY_H__
#define __CHRONOSRT_SCHED_POLICY_H__

#include <pthread.h>
#include <sys/types.h>

void chronosrt_sched_policy_init(void);

void chronosrt_sched_policy_set_scheduler(pid_t tid);
void chronosrt_sched_policy_set_runnable(pid_t tid);
void chronosrt_sched_policy_set_barrier(pid_t tid);
void chronosrt_sched_policy_set_suspended(pid_t tid);

void chronosrt_sched_policy_set_scheduler_p(pthread_t thread);
void chronosrt_sched_policy_set_runnable_p(pthread_t thread);
void chronosrt_sched_policy_set_barrier_p(pthread_t thread);
void chronosrt_sched_policy_set_suspended_p(pthread_t thread);

#endif

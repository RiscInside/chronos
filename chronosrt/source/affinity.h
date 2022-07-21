#ifndef __CHRONOSRT_AFFINITY_H__
#define __CHRONOSRT_AFFINITY_H__

#include <pthread.h>
#include <sys/types.h>

#include <chronosrt/cpuset.h>

void chronosrt_set_affinity(pid_t tid, chronosrt_cpuset_t *set);

void chronosrt_set_affinity_p(pthread_t thread, chronosrt_cpuset_t *set);

#endif

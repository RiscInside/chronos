#ifndef __CHRONOSRT_AFFINITY_H__
#define __CHRONOSRT_AFFINITY_H__

#include <sys/types.h>

#include <chronosrt/cpuset.h>

void chronosrt_set_affinity(pid_t pid, chronosrt_cpuset_t *set);

#endif

#ifndef __CHRONOS_CPUSET_H__
#define __CHRONOS_CPUSET_H__

#include <pthread.h>
#include <sched.h>
#include <stdbool.h>
#include <stddef.h>
#include <sys/types.h>

struct chronos_cpuset;

typedef struct chronos_cpuset *chronos_cpuset_t;

chronos_cpuset_t chronos_cpuset_create_raw(size_t cpus);
chronos_cpuset_t chronos_cpuset_create_empty(size_t cpus);
chronos_cpuset_t chronos_cpuset_create_with_one_set(size_t cpus, size_t one_set);

inline static chronos_cpuset_t chronos_cpuset_with_one_cpu(size_t cpu) {
	return chronos_cpuset_create_with_one_set(cpu + 1, cpu);
}

void chronos_cpuset_include_cpu(chronos_cpuset_t cpuset, size_t cpu);
void chronos_cpuset_exclude_cpu(chronos_cpuset_t cpuset, size_t cpu);
bool chronos_cpuset_is_cpu_included(chronos_cpuset_t cpuset, size_t cpu);
bool chronos_cpuset_get_next_enabled(chronos_cpuset_t cpuset, size_t *current);

size_t chronos_cpuset_count_enabled(chronos_cpuset_t cpuset);

void chronos_cpuset_destroy(chronos_cpuset_t cpuset);

void chronos_cpuset_set_pthread_affinity(pthread_t thread, chronos_cpuset_t cpuset);
void chronos_cpuset_set_tid_affinity(pid_t thread, chronos_cpuset_t cpuset);

#endif

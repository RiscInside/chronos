#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <chronos/cpuset.h>
#include <log.h>
#include <sched.h>
#include <stdlib.h>

struct chronos_cpuset {
	size_t cpus_count;
	size_t cpu_alloc_size;
	cpu_set_t *set;
};

chronos_cpuset_t chronos_cpuset_create_raw(size_t cpus) {
	cpu_set_t *set = CPU_ALLOC(cpus);
	chronos_assert(set);
	chronos_cpuset_t result = malloc(sizeof(struct chronos_cpuset));
	chronos_assert(result);
	result->cpus_count = cpus;
	result->cpu_alloc_size = CPU_ALLOC_SIZE(cpus);
	result->set = set;
	return result;
}

chronos_cpuset_t chronos_cpuset_create_empty(size_t cpus) {
	chronos_cpuset_t res = chronos_cpuset_create_raw(cpus);
	CPU_ZERO_S(res->cpu_alloc_size, res->set);
	return res;
}

chronos_cpuset_t chronos_cpuset_create_with_one_set(size_t cpus, size_t one_set) {
	chronos_cpuset_t res = chronos_cpuset_create_empty(cpus);
	chronos_cpuset_include_cpu(res, one_set);
	return res;
}

void chronos_cpuset_include_cpu(chronos_cpuset_t cpuset, size_t cpu) {
	chronos_assert(cpu < cpuset->cpus_count);
	CPU_SET_S(cpu, cpuset->cpu_alloc_size, cpuset->set);
}

void chronos_cpuset_exclude_cpu(chronos_cpuset_t cpuset, size_t cpu) {
	chronos_assert(cpu < cpuset->cpus_count);
	CPU_CLR_S(cpu, cpuset->cpu_alloc_size, cpuset->set);
}

bool chronos_cpuset_is_cpu_included(chronos_cpuset_t cpuset, size_t cpu) {
	chronos_assert(cpu < cpuset->cpus_count);
	return CPU_ISSET_S(cpu, cpuset->cpu_alloc_size, cpuset->set);
}

bool chronos_cpuset_get_next_enabled(chronos_cpuset_t cpuset, size_t *current) {
	for (; *current < cpuset->cpus_count; ++(*current)) {
		if (chronos_cpuset_is_cpu_included(cpuset, *current)) {
			return true;
		}
	}
	return false;
}

void chronos_cpuset_destroy(chronos_cpuset_t cpuset) {
	CPU_FREE(cpuset->set);
	free(cpuset);
}

size_t chronos_cpuset_count_enabled(chronos_cpuset_t cpuset) {
	size_t result = 0;
	for (size_t i = 0; i < cpuset->cpus_count; ++i) {
		if (chronos_cpuset_is_cpu_included(cpuset, i)) {
			result++;
		}
	}
	return result;
}

void chronos_cpuset_set_pthread_affinity(pthread_t thread, chronos_cpuset_t cpuset) {
	chronos_assert_false(pthread_setaffinity_np(thread, cpuset->cpu_alloc_size, cpuset->set));
}

void chronos_cpuset_set_tid_affinity(pid_t tid, chronos_cpuset_t cpuset) {
	chronos_assert_false(sched_setaffinity(tid, cpuset->cpu_alloc_size, cpuset->set));
}

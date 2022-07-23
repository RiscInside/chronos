#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <sched.h>
#include <stdlib.h>

#include <affinity.h>
#include <chronosrt/cpuset.h>
#include <fail.h>

struct chronosrt_cpuset_struct {
	cpu_set_t *cpuset;
	size_t cpu_alloc_size;
	size_t cpus;
};

chronosrt_cpuset_t *chronosrt_cpuset_create(size_t cpus) {
	struct chronosrt_cpuset_struct *cpuset = malloc(sizeof(struct chronosrt_cpuset_struct));
	CHRONOSRT_ASSERT_TRUE(cpuset);
	cpuset->cpuset = CPU_ALLOC(cpus);
	CHRONOSRT_ASSERT_TRUE(cpuset->cpuset);
	cpuset->cpu_alloc_size = CPU_ALLOC_SIZE(cpus);
	CPU_ZERO_S(cpuset->cpu_alloc_size, cpuset->cpuset);
	cpuset->cpus = cpus;
	return cpuset;
}

void chronosrt_cpuset_copy(chronosrt_cpuset_t *src, chronosrt_cpuset_t *dst) {
	for (size_t i = 0; i < dst->cpus; ++i) {
		chronosrt_cpuset_set(dst, i, chronosrt_cpuset_get(src, i));
	}
}

chronosrt_cpuset_t *chronosrt_cpuset_create_copy(chronosrt_cpuset_t *other) {
	chronosrt_cpuset_t *cpuset = chronosrt_cpuset_create(other->cpus);
	chronosrt_cpuset_copy(cpuset, other);
	return cpuset;
}

chronosrt_cpuset_t *chronosrt_cpuset_create_with_one_set(size_t cpu) {
	struct chronosrt_cpuset_struct *cpuset = chronosrt_cpuset_create(cpu + 1);
	chronosrt_cpuset_set(cpuset, cpu, 1);
	return cpuset;
}

int chronosrt_cpuset_get(chronosrt_cpuset_t *set, size_t cpu) {
	CHRONOSRT_ASSERT_TRUE(cpu < set->cpus);
	return CPU_ISSET_S(cpu, set->cpu_alloc_size, set->cpuset);
}

void chronosrt_cpuset_set(chronosrt_cpuset_t *set, size_t cpu, int on) {
	CHRONOSRT_ASSERT_TRUE(cpu < set->cpus);
	if (on) {
		CPU_SET_S(cpu, set->cpu_alloc_size, set->cpuset);
	} else {
		CPU_CLR_S(cpu, set->cpu_alloc_size, set->cpuset);
	}
}

void chronosrt_cpuset_zero(chronosrt_cpuset_t *set) {
	CPU_ZERO_S(set->cpu_alloc_size, set->cpuset);
}

void chronosrt_cpuset_destroy(chronosrt_cpuset_t *set) {
	CPU_FREE(set->cpuset);
	free(set);
}

void chronosrt_set_affinity(pid_t tid, chronosrt_cpuset_t *set) {
	CHRONOSRT_ASSERT_FALSE(sched_setaffinity(tid, set->cpu_alloc_size, set->cpuset));
}

void chronosrt_set_affinity_p(pthread_t thread, chronosrt_cpuset_t *set) {
	CHRONOSRT_ASSERT_FALSE(pthread_setaffinity_np(thread, set->cpu_alloc_size, set->cpuset));
}

size_t chronosrt_cpuset_size(chronosrt_cpuset_t *set) {
	return set->cpus;
}

size_t chronosrt_cpuset_count(chronosrt_cpuset_t *set) {
	size_t res = 0;
	for (size_t i = 0; i < set->cpus; ++i) {
		if (chronosrt_cpuset_get(set, i)) {
			res++;
		}
	}
	return res;
}

size_t chronosrt_cpuset_first_set(chronosrt_cpuset_t *set) {
	for (size_t i = 0; i < set->cpus; ++i) {
		if (chronosrt_cpuset_get(set, i)) {
			return i;
		}
	}
	return set->cpus;
}

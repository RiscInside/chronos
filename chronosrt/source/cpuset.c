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
};

chronosrt_cpuset_t *chronosrt_cpuset_create(size_t max_cpus) {
	struct chronosrt_cpuset_struct *cpuset = malloc(sizeof(struct chronosrt_cpuset_struct));
	CHRONOSRT_ASSERT_TRUE(cpuset);
	cpuset->cpuset = CPU_ALLOC(max_cpus);
	CHRONOSRT_ASSERT_TRUE(cpuset->cpuset);
	cpuset->cpu_alloc_size = CPU_ALLOC_SIZE(max_cpus);
}

int chronosrt_cpuset_get(chronosrt_cpuset_t *set, size_t cpu) {
	return CPU_ISSET_S(cpu, set->cpu_alloc_size, set->cpuset);
}

void chronosrt_cpuset_set(chronosrt_cpuset_t *set, size_t cpu, int on) {
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

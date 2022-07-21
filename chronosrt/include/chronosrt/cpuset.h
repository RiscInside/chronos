#ifndef __CHRONOSRT_CPUSET_H__
#define __CHRONOSRT_CPUSET_H__

#include <stddef.h>

typedef struct chronosrt_cpuset_struct chronosrt_cpuset_t;

chronosrt_cpuset_t *chronosrt_cpuset_create(size_t cpus);

chronosrt_cpuset_t *chronosrt_cpuset_create_with_one_set(size_t cpu);

chronosrt_cpuset_t *chronosrt_cpuset_copy(chronosrt_cpuset_t *other);

int chronosrt_cpuset_get(chronosrt_cpuset_t *set, size_t cpu);

void chronosrt_cpuset_set(chronosrt_cpuset_t *set, size_t cpu, int on);

void chronosrt_cpuset_zero(chronosrt_cpuset_t *set);

void chronosrt_cpuset_destroy(chronosrt_cpuset_t *set);

size_t chronos_cpuset_size(chronosrt_cpuset_t *set);

#endif

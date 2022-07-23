#ifndef __CHRONOSRT_CHRONOSRT_H__
#define __CHRONOSRT_CHRONOSRT_H__

#include <chronosrt/cpuset.h>

struct chronosrt_cfg {
	/* CPUs chronosrt can use to schedule processes */
	chronosrt_cpuset_t *cpuset;

	/* Minimal timeslice in nanoseconds */
	size_t ns_min_timeslice;

	/* How far ahead tasks can be in vruntime */
	size_t max_vruntime_diff;
};

void chronosrt_instantiate_runtime(struct chronosrt_cfg *cfg);

void chronosrt_enter_critical(void);
void chronosrt_exit_critical(void);

// Must be called from within the critical section
void *chronosrt_new_thread_token(void);

void chronosrt_attach(void *token);

void chronosrt_detach(void);

void chronosrt_set_tdf(double new_tdf);

size_t chronosrt_get_virtual_time(void);

#endif

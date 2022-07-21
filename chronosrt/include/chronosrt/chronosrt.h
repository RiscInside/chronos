#ifndef __CHRONOSRT_CHRONOSRT_H__
#define __CHRONOSRT_CHRONOSRT_H__

#include <chronosrt/cpuset.h>

struct chronosrt_cfg {
	/* CPUs chronosrt can use to schedule processes */
	chronosrt_cpuset_t *cpuset;

	/* Minimal timeslice in nanoseconds */
	size_t ns_min_timeslice;
};

void chronosrt_instantiate_runtime(struct chronosrt_cfg *cfg);

void chronosrt_set_tdf(double new_tdf);

void *chronosrt_new_thread_token(void);

void chronosrt_attach_to_runtime(void *token);

void chronosrt_detatch_from_runtime();

#endif

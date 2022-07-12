#pragma once

// "chronos.h" - chronos library interface
#include <pthread.h>
#include <sched.h>

// Experiment. Threads can register with experiment via chronos_register function
struct chronos_experiment {
	// cpu mask
	cpu_set_t *available_cpus;
};

// Initialize chronos experiment
int chronos_experiment_init(struct chronos_experiment *experiment, cpu_set_t *available_cpus);

// Cleanup chronos experiment
void chronos_experiment_cleanup(struct chronos_experiment *experiment);

// Create chronos managed thread
int chronos_thread_create(struct chronos_experiment *experiment, pthread_t *restrict thread,
                          const pthread_attr_t *restrict attr, void *(*start_routine)(void *), void *restrict arg);

// Set time dilation factor percentage (100 is a no-op, 200 will make code run 2 times faster in virtual time)
int chronos_set_tdf(struct chronos_experiment *experiment, int percentage);

// Reset time dilation factor
inline static void chronos_reset_tdf(struct chronos_experiment *experiment, int percentage) {
	chronos_set_tdf(experiment, 100);
}

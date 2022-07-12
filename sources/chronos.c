#define _GNU_SOURCE

#include <chronos.h>
#include <errno.h>
#include <sched.h>
#include <stdlib.h>
#include <unistd.h>

// Chronos thread-local state
struct chronos_tcb {
	// Pointer to the chronos experiment
	struct chronos_experiment *experiment;
	// Argument to pass to the thread (as in pthread-create)
	void *arg;
	// Start routine
	void *(*start_routine)(void *);
};

// Pointer to the thread-local state
static __thread struct chronos_tcb *chronos_tcb = NULL;

// Initialize chronos experiment
int chronos_experiment_init(struct chronos_experiment *experiment, cpu_set_t *available_cpus) {
	experiment->available_cpus = CPU_ALLOC(CPU_SETSIZE);
	if (experiment->available_cpus == NULL) {
		return ENOMEM;
	}
	return 0;
}

// Cleanup chronos experiment
void chronos_experiment_cleanup(struct chronos_experiment *experiment) {
	CPU_FREE(experiment->available_cpus);
}

// Thread trampoline
void *chronos_trampoline(struct chronos_experiment *experiment, struct chronos_tcb *state) {
	// Point thread to its thread-local state
	chronos_tcb = state;
	// Run the callback
	void *res = state->start_routine(state->arg);
}

// Create chronos managed thread
int chronos_thread_create(struct chronos_experiment *experiment, pthread_t *restrict thread,
                          const pthread_attr_t *restrict attr, void *(*start_routine)(void *), void *restrict arg) {
	struct chronos_tcb *state = malloc(sizeof(struct chronos_tcb));
	if (!state) {
		return ENOMEM;
	}
	state->arg = arg;
	state->start_routine = start_routine;
	return pthread_create(thread, attr, (void *)chronos_trampoline, state);
}

// Set time dilation factor percentage (100 is a no-op, 200 will make code run 2 times faster in virtual time)
int chronos_set_tdf(struct chronos_experiment *experiment, int percentage) {
}

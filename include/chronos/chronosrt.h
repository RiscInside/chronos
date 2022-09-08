#ifndef __CHRONOS_CHRONOSRT_H__
#define __CHRONOS_CHRONOSRT_H__

#include <chronos/cpuset.h>
#include <pthread.h>
#include <stddef.h>
#include <sys/types.h>

// Config for the Chronos runtime
struct chronosrt_config {
	// CPUs runtime can use
	chronos_cpuset_t available_cpus;
	// Minimum local timeslice length
	size_t min_local_timeslice;
	// Suspend lag allowance - scheduler suspends virtual CPUs that are suspend_lag_allowance ns ahead of some other
	// vCPU
	size_t suspend_lag_allowance;
	// Resume lag allowance - scheduler resumes virtual CPUs that are at most resume_lag_allowance ns ahead of other
	// CPUs. Must be < suspend_lag_allowance
	size_t resume_lag_allowance;
	// Rescheduling penalty
	size_t reschedule_penalty;
	// Barriers deadline
	size_t barriers_deadline;
	// Runtime mode
	enum chronosrt_runtime_mode {
		// No-op (NOP) mode. Ignore all framework calls
		NOP,
		// Events only mode. Most framework calls are ignored, event scheduling is simulated however
		EVENTS_ONLY,
		// Full mode. All framework calls are taken into account
		FULL,
	} runtime_mode;
};

// Instantiate chronos runtime with a given configuration
void chronosrt_instantiate(struct chronosrt_config *config);

// Create new thread control block (usually called before clone() or pthread_create())
void *chronosrt_new_tcb(void);

// Call after clone() to submit a thread to the scheduler
// NOTE: TCBs can be created with chronosrt_new_tcb()
void chronosrt_on_parent_thread_hook(void *tcb, pid_t pid);

// Call after pthread_create() to submit a thread to the scheduler
// NOTE: TCBs can be created with chronosrt_new_tcb()
void chronosrt_on_parent_thread_hook_p(void *tcb, pthread_t pthread);

// Use a given TCB on this thread. Needs to be called before calling any other API functions on this this
// thread
void chronosrt_on_child_thread_hook(void *tcb);

// Set time dilation factor
void chronosrt_set_tdf(double new_tdf);

// Set virtual time of this thread to a given value. Returns false if value is lower than thread's current virtual time
bool chronosrt_set_virtual_time(size_t new_virtual_time);

// Advance virtual time of the thread by a given amount
void chronosrt_advance_virtual_time_by(size_t amount);

// Timed section represents a part of program execution that has predetermined execution time. These timed sections
// are useful for all kinds of mock models
struct chronosrt_timed_section {
	size_t barrier_virtual_deadline;
	size_t barrier_real_deadline;
};

// Begin new timed section. "old_tos" is used as a scratch space to allow for nested timed sections
void chronosrt_begin_timed_section(size_t limit, struct chronosrt_timed_section *old_tos);

// Restore old virtual time barrier
void chronosrt_end_timed_section(struct chronosrt_timed_section *old_tos);

// Get thread's runtime. Equivalent to CLOCK_THREAD_CPUTIME_ID
size_t chronosrt_get_thread_running_time(void);

// Get simulation time. Equivalent to gettimeofday() - simulation start time
size_t chronosrt_get_sim_time(void);

// Detach current thread to the framework
void chronosrt_on_exit_thread(void);

// Chronos runtime equivalent of sched_yield()
void chronosrt_yield(void);

// Calculate average scheduler main loop duration. Use for debugging
size_t chronosrt_calc_avg_loop_duration(void);

#endif

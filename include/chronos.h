#pragma once

#include <sched.h>
#include <time.h>

// "chronos.h" - chronos library interface

// Instantiate chronos runtime. This routine
// 1) Creates a scheduler thread that keeps track of vruntimes of all threads in experiment and maps threads to CPUs
// 2) Sets the priority of this thread to SCHED_FIFO and submits it to the scheduler
// 3) Sets the time dilation factor of this thread to 1
// Chronos runtime self-destructs automatically when no thread that belongs to said runtime is running
// cpu_set_t * is used to limit cpus scheduler can use. Scheduler will run threads with SCHED_FIFO priority, so its a
// good idea to leave some bits clear. At least two bits should be set however - one CPU is reserved for the scheduler
// thread that runs with SCHED_FIFO priority as well
// vruntime_step specifies scheduler precision: chronos will attempt to run threads for vruntime_step during each round
// This function returns a runtime token that can be used to submit other threads to the runtime
void *chronos_rt_init(size_t cpusetsize, cpu_set_t const *mask, size_t vruntime_step);

// Submit current thread to the chronos runtime
void chronos_rt_submit(void *rt_token, size_t initial_vruntime);

// Detatch current thread from the chronos runtime
// Call before exit()
void chronos_rt_detatch();

// Set time dilation factor (1.0 - no/op, 2.0 - 2 times faster, 0.5 - 2 times slower)
void chronos_set_tdf(double tdf);

// Get vruntime
size_t chronos_get_vruntime();

// Jump delta_ns nanoseconds forward in virtual time
void chronos_jump(size_t delta_ns);

// Enter critical section (disable rescheduling). Returns critical section id that is to be passed to
// chronos_exit_critical
int chronos_enter_critical();

// Exit critical section (enable rescheduling). Accepts critical section id from chronos_enter_critical
void chronos_exit_critical(int section_id);

// Check that vruntime is wtihin bounds and preempt if necessary. Won't have effect until outermost critical section is
// exited.
void chronos_vruntime_check();

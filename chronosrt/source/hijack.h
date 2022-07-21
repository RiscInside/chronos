#pragma once

#include <pthread.h>
#include <sched.h>
#include <semaphore.h>
#include <stddef.h>

// "hijack.h" - interface to the "hijacker" (name matches the spirit), a facility to suspend/resume thread execution
// on a given core. "hijacker" attempts to completely hijack CPU from the Linux scheduler, allowing other tasks to run
// only if the thread hijacker runs suspends.
// "hijacker" also implements thread freeze functionality, that allows to *almost* pause execution of runnable threads
// on a best effort basis.

// "hijacker" instance on a given CPU
struct hijack_cpu {
	// CPU "hijacker" runs on
	size_t cpu;
	// Freezer barrier thread handle
	pthread_t freezer_barrier;
	// CPU mask
	cpu_set_t *cpuset;
};

// Hijack a given CPU. rt_prio specifies the priority as in sched_setscheduler system call
void hijack_init(struct hijack_cpu *info, size_t cpu_id);

// Pin task with a given tid to the hijacked CPU. Freeze beforehand for best results
void hijack_pin(struct hijack_cpu *info, pid_t pid);

// "freeze" task.
void hijack_freeze(struct hijack_cpu *info, pid_t pid);

// "unfreeze" task and run its with a given real-time priority
void hijack_unfreeze(struct hijack_cpu *info, pid_t pid, int rt_prio);

// Return control of the CPU to the system, deinitializing the hijacker runtime
void hijack_deinit(struct hijack_cpu *info);

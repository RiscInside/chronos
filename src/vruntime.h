#ifndef __CHRONOSRT_VRUNTIME_H__
#define __CHRONOSRT_VRUNTIME_H__

#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>

#define TDF_UNIT 1024

// Vruntime pair is in the core of vruntime calculations. It has two values
// 1. Last computed vruntime
// 2. Real time of the computation
// Virtual time is updated with a double compare-and-swap operation on vruntime pair structure
// This ensures that vruntime updates are always consistent
struct chronos_vruntime_pair {
	size_t last_committed_vruntime;
	size_t last_update_real;
};

// Vruntime barrier allows to prevent virtual time passage beyond a certain point
// Deadlines are needed to resolve deadlocks
struct chronos_vruntime_barrier {
	size_t barrier_virtual;
	size_t barrier_real_deadline;
};

// Vruntime structure has a vruntime pair and a set of parameters used for vruntime delta calculations
struct chronos_vruntime {
	_Atomic struct chronos_vruntime_pair pair;
	_Atomic struct chronos_vruntime_barrier barrier;
	atomic_int tdf;
	size_t barrier_real_max;
};

// Initialize vruntime accounting struct and set vruntime to a specific value
// barrier_real_max specifies how long can code inside barrier last
void chronos_vruntime_init(struct chronos_vruntime *vruntime, size_t current_time, size_t barrier_real_max,
                           size_t initial_vruntime);

// Update vruntime statistics using new real timestamp
void chronos_vruntime_update(struct chronos_vruntime *vruntime, size_t new_real_time);

// Push barrier on the barrier "stack".
// NOTE: barriers prevent virtual time progression beyond a certain point
// NOTE: current is used as a scratch space to store current barrier. Nested chronos_vruntime_barrier_push() calls
// create an implicit barrier stack with lower and lower limits
// NOTE: if limit is greater than TOS limit, barrier limit will be clamped
void chronos_vruntime_barrier_push(struct chronos_vruntime *vruntime, size_t limit,
                                   struct chronos_vruntime_barrier *old_tos, size_t current_real_time);

// Pop barrier from the stack, allowing virtual time to progress further. Returns limit of the popped barrier
size_t chronos_vruntime_barrier_pop(struct chronos_vruntime *vruntime, const struct chronos_vruntime_barrier *old_tos);

// Set time dilation factor. This functions allows to speed up code by a certain factor
void chronos_vruntime_set_tdf(struct chronos_vruntime *vruntime, double tdf, size_t new_real_time);

// Get last comitted vruntime. Use to prevent unnecessary updates
size_t chronos_vruntime_get_committed(struct chronos_vruntime *vruntime);

// Equivalent of chronos_vruntime_update() + chronos_vruntime_get_comitted()
size_t chronos_vruntime_get(struct chronos_vruntime *vruntime, size_t new_real_time);

// Return value for vruntime updates
enum chronos_vruntime_update_status {
	CHRONOS_VRUNTIME_SUCCESS,
	CHRONOS_VRUNTIME_RETRY_UPDATE,
	CHRONOS_VRUNTIME_TIMESTAMP_BEHIND,
};

// Try to set virtual time to some specific value. Use with caution
enum chronos_vruntime_update_status chronos_vruntime_try_set(struct chronos_vruntime *vruntime, size_t new_virtual_time,
                                                             size_t new_real_time);

// Advance virtual time by a given number of nanoseconds. Can be used for mock performance modelling
bool chronos_vruntime_try_advance_by(struct chronos_vruntime *vruntime, size_t diff, size_t new_real_time);

// Update last calculation timestamp without updating real-time
// Equivalent of chronos_vruntime_set(vruntime, chronos_vruntime_get_uncomitted(vruntime))
// NOTE: this operation assumes that no other vruntime operations are running concurrently
void chronos_vruntime_update_real(struct chronos_vruntime *vruntime, size_t new_real_time);

#endif

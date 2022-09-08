#include <log.h>
#include <stdint.h>
#include <vruntime.h>

// Initialize vruntime accounting struct and set vruntime to a specific value
// barrier_real_max specifies how long can code inside barrier last
void chronos_vruntime_init(struct chronos_vruntime *vruntime, size_t current_time, size_t barrier_real_max,
                           size_t initial_vruntime) {
	// Init vruntime calculation parameters
	vruntime->barrier_real_max = barrier_real_max;
	atomic_store(&vruntime->tdf, TDF_UNIT);
	// Create default barrier. Default barrier never expires in virtual time and lives forever
	struct chronos_vruntime_barrier default_barrier;
	default_barrier.barrier_virtual = SIZE_MAX;
	default_barrier.barrier_real_deadline = SIZE_MAX;
	atomic_store(&vruntime->barrier, default_barrier);
	// Init values in vruntime pair
	struct chronos_vruntime_pair init_pair;
	init_pair.last_committed_vruntime = initial_vruntime;
	init_pair.last_update_real = current_time;
	atomic_store(&vruntime->pair, init_pair);
}

// Apply current barrier to the virtual timestamp and return corrected value
static size_t chronos_vruntime_apply_barrier(struct chronos_vruntime *vruntime, size_t val,
                                             struct chronos_vruntime_pair prev_pair) {
	// Load barrier parameters
	struct chronos_vruntime_barrier barrier = atomic_load(&vruntime->barrier);
	// Check if we passed the barrier already
	size_t prev_vruntime = prev_pair.last_committed_vruntime;
	size_t prev_update_real = prev_pair.last_update_real;
	if (prev_vruntime >= barrier.barrier_virtual) {
		// If barrier has already been reached before the update, we block the update
		// We first need to check for the barrier timeout however
		// NOTE: since all barriers share one common lifespan, expiration of TOS deadline means that deadlines of all
		// barriers have expired
		if (prev_update_real > barrier.barrier_real_deadline) {
			// Barrier's deadline has expired, hence we break the barrier and allow virtual time to progress
			return val;
		}
		// No deadlock, but we don't want to go back in virtual time. Return prev_vruntime.
		return prev_vruntime;
	}
	// NOTE: if barrier_real_deadline is reached at this point, vruntime will be set to barrier.barrier_virtual and the
	// above if will pick up deadline expiration on next vruntime update
	return barrier.barrier_virtual < val ? barrier.barrier_virtual : val;
}

// Precompute new vruntime pair from the old one and current real timestamp
static struct chronos_vruntime_pair chronos_vruntime_dry_update(struct chronos_vruntime *vruntime, size_t new_real_time,
                                                                struct chronos_vruntime_pair old_pair) {
	// Update procedure works by subtracting real time between last and this updates and using TDF to calculate how much
	// time has passed in virtual time. Barrier is then applied with chronos_vruntime_apply_barrier() function
	chronos_assert(new_real_time >= old_pair.last_update_real);
	size_t real_time_diff = new_real_time - old_pair.last_update_real;
	size_t vruntime_diff = (real_time_diff * TDF_UNIT) / atomic_load(&vruntime->tdf);
	size_t new_vruntime = old_pair.last_committed_vruntime + vruntime_diff;
	new_vruntime = chronos_vruntime_apply_barrier(vruntime, new_vruntime, old_pair);

	struct chronos_vruntime_pair new_pair;
	new_pair.last_committed_vruntime = new_vruntime;
	new_pair.last_update_real = new_real_time;
	return new_pair;
}

// Update vruntime statistics using new real timestamp
void chronos_vruntime_update(struct chronos_vruntime *vruntime, size_t new_real_time) {
	struct chronos_vruntime_pair old_pair = atomic_load(&vruntime->pair);
	if (old_pair.last_update_real >= new_real_time) {
		// Someone has updated struct after us
		return;
	}
	struct chronos_vruntime_pair new_pair = chronos_vruntime_dry_update(vruntime, new_real_time, old_pair);
	// Now we commit our changes with compare_exchange_strong
	// NOTE: if cmpxchg fails, we don't have to retry, as we are simply racing with other update calls that will do
	// update for us
	atomic_compare_exchange_strong(&vruntime->pair, &old_pair, new_pair);
}

// Push barrier on the barrier "stack".
// NOTE: barriers prevent virtual time progression beyond a certain point
// NOTE: current is used as a scratch space to store current barrier. Nested chronos_vruntime_barrier_push() calls
// create an implicit barrier stack with lower and lower limits
// NOTE: if limit is greater than TOS limit, barrier limit will be clamped
void chronos_vruntime_barrier_push(struct chronos_vruntime *vruntime, size_t limit,
                                   struct chronos_vruntime_barrier *old_tos, size_t current_real_time) {
	*old_tos = atomic_load(&vruntime->barrier);
	struct chronos_vruntime_barrier new_tos;
	new_tos.barrier_virtual = old_tos->barrier_virtual < limit ? old_tos->barrier_virtual : limit;
	new_tos.barrier_real_deadline = current_real_time + vruntime->barrier_real_max;
	atomic_store(&vruntime->barrier, new_tos);
}

// Pop barrier from the stack, allowing virtual time to progress further. Returns limit of the popped barrier
size_t chronos_vruntime_barrier_pop(struct chronos_vruntime *vruntime, const struct chronos_vruntime_barrier *old_tos) {
	struct chronos_vruntime_barrier current_tos = atomic_load(&vruntime->barrier);
	atomic_store(&vruntime->barrier, *old_tos);
	return current_tos.barrier_virtual;
}

// Set time dilation factor
void chronos_vruntime_set_tdf(struct chronos_vruntime *vruntime, double tdf, size_t new_real_time) {
	size_t tdf_in_units = (size_t)(tdf * (double)TDF_UNIT);
	chronos_vruntime_update(vruntime, new_real_time);
	atomic_store(&vruntime->tdf, tdf_in_units);
}

// Get last comitted vruntime. Use to prevent unnecessary updates
size_t chronos_vruntime_get_committed(struct chronos_vruntime *vruntime) {
	struct chronos_vruntime_pair pair = atomic_load(&vruntime->pair);
	return pair.last_committed_vruntime;
}
// Equivalent of chronos_vruntime_update() + chronos_vruntime_get_comitted()
size_t chronos_vruntime_get(struct chronos_vruntime *vruntime, size_t new_real_time) {
	chronos_vruntime_update(vruntime, new_real_time);
	return chronos_vruntime_get_committed(vruntime);
}

// Try to set virtual time to some specific value. Use with caution
enum chronos_vruntime_update_status chronos_vruntime_try_set(struct chronos_vruntime *vruntime, size_t new_virtual_time,
                                                             size_t new_real_time) {
	struct chronos_vruntime_pair pair = atomic_load(&vruntime->pair);
	struct chronos_vruntime_pair new_pair;
	if (pair.last_committed_vruntime >= new_virtual_time) {
		return CHRONOS_VRUNTIME_TIMESTAMP_BEHIND;
	}
	new_pair.last_committed_vruntime = chronos_vruntime_apply_barrier(vruntime, new_virtual_time, pair);
	new_pair.last_update_real = new_real_time;
	if (atomic_compare_exchange_weak(&vruntime->pair, &pair, new_pair)) {
		return CHRONOS_VRUNTIME_SUCCESS;
	}
	return CHRONOS_VRUNTIME_RETRY_UPDATE;
}

// Advance virtual time by a given number of nanoseconds. Can be used for mock performance modelling
bool chronos_vruntime_try_advance_by(struct chronos_vruntime *vruntime, size_t diff, size_t new_real_time) {
	struct chronos_vruntime_pair old_pair = atomic_load(&vruntime->pair);
	if (old_pair.last_update_real >= new_real_time) {
		return false;
	}
	struct chronos_vruntime_pair new_pair = chronos_vruntime_dry_update(vruntime, new_real_time, old_pair);
	new_pair.last_committed_vruntime += diff;
	if (atomic_compare_exchange_weak(&vruntime->pair, &old_pair, new_pair)) {
		return false;
	}
	return true;
}

// Update last calculation timestamp without updating real-time
// Equivalent of chronos_vruntime_set(vruntime, chronos_vruntime_get_uncomitted(vruntime))
// NOTE: this operation assumes that no other vruntime operations are running concurrently
void chronos_vruntime_update_real(struct chronos_vruntime *vruntime, size_t new_real_time) {
	struct chronos_vruntime_pair old_pair = atomic_load(&vruntime->pair);
	struct chronos_vruntime_pair new_pair;
	new_pair.last_update_real = new_real_time;
	new_pair.last_committed_vruntime = old_pair.last_committed_vruntime;
	atomic_store(&vruntime->pair, new_pair);
}

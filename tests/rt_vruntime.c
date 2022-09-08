#include <assert.h>
#include <log.h>
#include <ns.h>
#include <stdint.h>
#include <stdio.h>
#include <test_helpers.h>
#include <vruntime.h>

// This is a test for virtual time accounting functions. These are the driver of virtual time simulation framework: all
// virtual time related parameters (the most important of course being time dilation factor) are reported to the
// vruntime accounting code. The only thing left for scheduler is to see if there are significant disparancies in
// simulation time between cores caused by virtual time parameters.

int main() {
	// Initialize vruntime struct and set virtual time value to 0
	struct chronos_vruntime vruntime;
	chronos_vruntime_init(&vruntime, chronos_ns_get_thread(), SIZE_MAX, 0);

	// Waste 10 milliseconds of CPU time
	chronos_spin_for_ms(10);

	// No virtual time trickery used yet, so we should get something like 10ms
	size_t timestamp = chronos_vruntime_get(&vruntime, chronos_ns_get_thread());
	chronos_test_log("Virtual time after 10ms is %zu", timestamp);

	// Now we set time dilation factor to two and waste 20 more milliseconds of CPU time
	chronos_vruntime_set_tdf(&vruntime, 2.0, chronos_ns_get_thread());
	chronos_spin_for_ms(20);

	// Event though 20 milliseconds of real time has passed (for this thread anyway), the vruntime should only advance
	// by 10 milliseconds
	timestamp = chronos_vruntime_get(&vruntime, chronos_ns_get_thread());
	chronos_test_log("Virtual time after 20ms (with TDF 2.0) is %zu", timestamp);
	assert(timestamp < 21000000);
	assert(timestamp > 19000000);

	// Virtual time barriers is another way of manipulating virtual times. We can set a barrier to ensure that virtual
	// time will not pass a certain point, no matter how much time code will take. This is useful for timing models,
	// when we want to claim that certain portion of code takes a particular amount of time

	// Here we set a barrier to 45000000 nanoseconds (45 ms), meaning that virtual time will not pass this point
	struct chronos_vruntime_barrier barrier_buf;
	chronos_vruntime_barrier_push(&vruntime, 45000000, &barrier_buf, chronos_ns_get_thread());

	// Set time dilation factor to 1/2. Thread will now appear two times slower than it actually is
	chronos_vruntime_set_tdf(&vruntime, 0.5, chronos_ns_get_thread());

	// Waste another 20 milliseconds
	chronos_spin_for_ms(20);

	// As we have not removed the barrier yet, the virtual time will stay capped at 45000000
	timestamp = chronos_vruntime_get(&vruntime, chronos_ns_get_thread());
	chronos_test_log("Virtual time after 40ms (with TDF 0.5) is %zu", timestamp);
	assert(timestamp == 45000000);

	// Reset parameters
	chronos_vruntime_barrier_pop(&vruntime, &barrier_buf);
	chronos_vruntime_set_tdf(&vruntime, 1.0, chronos_ns_get_thread());

	// We can also set virtual time to specific values, emulating jumps in virtual time. Here we set it to 80ms, jumping
	// by 35ms amost instantly.
	chronos_vruntime_try_set(&vruntime, 80000000, chronos_ns_get_thread());

	// Waste another 10 milliseconds
	chronos_spin_for_ms(10);

	// We should get something close to 90 milliseconds now
	timestamp = chronos_vruntime_get(&vruntime, chronos_ns_get_thread());
	chronos_test_log("Virtual time after 10ms (with TDF 1.0) is %zu", timestamp);
	assert(timestamp > 89000000);
	assert(timestamp < 91000000);

	// Are virtual runtime updates cheap? Here we run 10000000 of them to see how slow/fast they are. On my computer it
	// takes 300ms to do this
	size_t timestamp_start = chronos_ns_get_thread();
	for (int i = 0; i < 10000000; ++i) {
		chronos_vruntime_update(&vruntime, chronos_ns_get_monotonic());
	}
	size_t timestamp_end = chronos_ns_get_thread();
	size_t timestamp_diff = timestamp_end - timestamp_start;
	chronos_test_log("10 million vruntime updates took %f seconds", ((double)timestamp_diff) / 1000000000.0);
}

#include <chronos/chronosrt.h>
#include <chronos/cpuset.h>
#include <log.h>
#include <test_helpers.h>

int main(int argc, char **argv) {
	chronos_test_init_runtime(&argc, argv);

	// Waste 200ms of the CPU time
	chronos_spin_for_ms(200);

	// What is our runtime?
	size_t vruntime = chronosrt_get_thread_running_time();
	chronos_test_log("Thread's runtime after %zu ns under framework is %zu", 200, vruntime);

	// What is our simulation time?
	size_t sim_time = chronosrt_get_sim_time();
	chronos_test_log("Simulation time after %zu ns under framework is %zu", 200, sim_time);

	// How much time it does it take for the main loop to run?
	size_t avg_loop_time = chronosrt_calc_avg_loop_duration();
	chronos_test_log("On average, it takes %zu nanoseconds for the main scheduler loop to complete", avg_loop_time);

	// Let's change the time dilation factor
	chronosrt_set_tdf(2.0);

	// Waste another 200ms of the CPU time (NOTE: in real time)
	chronos_spin_for_ms(200);

	// What is our runtime now?
	vruntime = chronosrt_get_thread_running_time();
	chronos_test_log("Thread's runtime after another %zu ns (with TDF of 2) is %zu", 200, vruntime);

	// What is our simulation time?
	sim_time = chronosrt_get_sim_time();
	chronos_test_log("Simulation time after another %zu ns (with TDF of 2) is %zu", 200, sim_time);

	// Does change in TDF affect scheduler's latency in any way?
	avg_loop_time = chronosrt_calc_avg_loop_duration();
	chronos_test_log("On average, it now takes %zu nanoseconds for the main scheduler loop to complete", avg_loop_time);

	chronosrt_on_exit_thread();
}

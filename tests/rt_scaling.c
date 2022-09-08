#include <chronos/chronosrt.h>
#include <chronos/cpuset.h>
#include <log.h>
#include <test_helpers.h>

void *other_thread(void *tcb) {
	// Call child thread hook
	chronosrt_on_child_thread_hook(tcb);

	// Slow down this thread by a factor of 2
	chronosrt_set_tdf(2);

	// Waste 200 ms of the CPU time
	chronos_spin_for_ms(200);

	// What is our thread's runtime?
	size_t vruntime = chronosrt_get_thread_running_time();
	chronos_test_log("Other thread's runtime after %zu ns under framework is %zu", 200, vruntime);

	// What is our simulation time?
	size_t sim_time = chronosrt_get_sim_time();
	chronos_test_log("Simulation time after %zu ns (as measured from the other thread) under framework is %zu", 200,
	                 sim_time);

	// Exit this thread
	chronosrt_on_exit_thread();
	return NULL;
}

int main(int argc, char **argv) {
	chronos_test_init_runtime(&argc, argv);

	// Waste 200ms of the CPU time
	chronos_spin_for_ms(200);

	// Spawn a new thread
	void *new_thread_tcb = chronosrt_new_tcb();
	pthread_t pthread;
	chronos_assert_false(pthread_create(&pthread, NULL, other_thread, new_thread_tcb));
	chronos_assert_false(pthread_detach(pthread));
	chronosrt_on_parent_thread_hook_p(new_thread_tcb, pthread);

	// Run for another 300 ms
	chronos_spin_for_ms(300);

	// What is our thread runtime?
	size_t vruntime = chronosrt_get_thread_running_time();
	chronos_test_log("Thread's runtime after %zu ns under framework is %zu", 500, vruntime);

	// What is our simulation time?
	size_t sim_time = chronosrt_get_sim_time();
	chronos_test_log("Simulation time after %zu ns under framework is %zu", 500, sim_time);

	// How much time it does it take for the main loop to run?
	size_t avg_loop_time = chronosrt_calc_avg_loop_duration();
	chronos_test_log("On average, it takes %zu nanoseconds for the main scheduler loop to complete", avg_loop_time);

	// Exit this thread
	chronosrt_on_exit_thread();
	return 0;
}

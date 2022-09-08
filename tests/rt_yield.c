#include <chronos/chronosrt.h>
#include <chronos/cpuset.h>
#include <log.h>
#include <test_helpers.h>

void *other_thread(void *tcb) {
	// Call child thread hook
	chronosrt_on_child_thread_hook(tcb);

	while (true) {
		chronosrt_yield();
	}
}

int main(int argc, char **argv) {
	chronos_test_init_runtime(&argc, argv);

	// Spawn a new thread
	void *new_thread_tcb = chronosrt_new_tcb();
	pthread_t pthread;
	chronos_assert_false(pthread_create(&pthread, NULL, other_thread, new_thread_tcb));
	chronos_assert_false(pthread_detach(pthread));
	chronosrt_on_parent_thread_hook_p(new_thread_tcb, pthread);

	chronos_spin_for_ms(100);

	// What is our runtime?
	size_t vruntime = chronosrt_get_thread_running_time();
	chronos_test_log("Thread's runtime after %zu ns under framework is %zu", 200, vruntime);

	// What is our simulation time?
	size_t sim_time = chronosrt_get_sim_time();
	chronos_test_log("Simulation time after %zu ns under framework is %zu", 200, sim_time);
}

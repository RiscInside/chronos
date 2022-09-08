#include <chronos/chronosrt.h>
#include <chronos/cpuset.h>
#include <log.h>
#include <test_helpers.h>

void *other_thread(void *tcb) {
	// Call child thread hook
	chronosrt_on_child_thread_hook(tcb);
	chronos_test_log("Runtime of other thread is now %zu", chronosrt_get_thread_running_time());

	// Create virtual time section for 300ms
	struct chronosrt_timed_section sect;
	chronosrt_begin_timed_section(300000000, &sect);

	// Create virtual time section for 100ms
	struct chronosrt_timed_section nested_sect;
	chronosrt_begin_timed_section(100000000, &nested_sect);

	// Run for 300 ms
	chronos_spin_for_ms(300);
	chronos_check_and_advance_evcnt(0);

	// End nested section
	chronosrt_end_timed_section(&nested_sect);
	chronos_test_log("Runtime of other thread is now %zu", chronosrt_get_thread_running_time());

	// Run for another 300 ms
	chronos_spin_for_ms(300);
	chronos_check_and_advance_evcnt(2);

	// End top section
	chronosrt_end_timed_section(&sect);
	chronos_test_log("Runtime of other thread is now %zu", chronosrt_get_thread_running_time());
}

int main(int argc, char **argv) {
	chronos_test_init_runtime(&argc, argv);

	// Spawn a new thread
	void *new_thread_tcb = chronosrt_new_tcb();
	pthread_t pthread;
	chronos_assert_false(pthread_create(&pthread, NULL, other_thread, new_thread_tcb));
	chronos_assert_false(pthread_detach(pthread));
	chronosrt_on_parent_thread_hook_p(new_thread_tcb, pthread);

	// Run for 150ms
	chronos_spin_for_ms(150);
	chronos_check_and_advance_evcnt(1);

	// Run for 200 ms
	chronos_spin_for_ms(200);
	chronos_check_and_advance_evcnt(3);
}

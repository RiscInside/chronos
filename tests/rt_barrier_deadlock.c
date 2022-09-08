#include <chronos/chronosrt.h>
#include <chronos/cpuset.h>
#include <log.h>
#include <test_helpers.h>

void *other_thread(void *tcb) {
	// Call child thread hook
	chronosrt_on_child_thread_hook(tcb);

	// Create virtual time section for 100ms
	struct chronosrt_timed_section sect;
	chronosrt_begin_timed_section(100000000, &sect);

	// Do not exit the critical section
	while (true) {
		asm volatile("");
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

	// Run for 200ms
	chronos_spin_for_ms(200);
}

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <chronos/chronosrt.h>
#include <chronos/cpuset.h>
#include <log.h>
#include <ns.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <test_helpers.h>
#include <unistd.h>

// The ultimate stress test: spawn 100 threads and watch them being failry scheduled
#define THREADS_COUNT 100
#define TEST_SEED 0xdead

double sample_tdfs[] = {0.25, 0.5, 0.75, 1, 1.25, 1.5, 2, 2.5};
atomic_size_t threads_with_tdf[sizeof(sample_tdfs) / sizeof(double)] = {0};
atomic_size_t goahead = 0;
atomic_size_t left = THREADS_COUNT;

// Per-thread work function
void thread_do_work() {
	// Generate "random" TDF
	size_t our_tdf_index = chronos_ns_get_monotonic() % (sizeof(sample_tdfs) / sizeof(double));
	atomic_fetch_add(threads_with_tdf + our_tdf_index, 1);

	chronosrt_set_tdf(sample_tdfs[our_tdf_index]);

	// Waste 200ms in this loop
	chronos_spin_for_ms(200);

	// Report termination
	size_t runtime = chronosrt_get_thread_running_time();
	size_t sim_time = chronosrt_get_sim_time();
	chronos_test_log("Thread %d (TDF = %f) has finished. Thread's runtime is %zu, current simulation time is %zu",
	                 gettid(), sample_tdfs[our_tdf_index], runtime, sim_time);

	chronosrt_set_tdf(1.0);
	atomic_fetch_sub(&left, 1);
}

void *other_thread(void *tcb) {
	// Call child thread hook
	chronosrt_on_child_thread_hook(tcb);

	// Wait for the main thread to finish spawning all other threads
	struct chronosrt_timed_section sect;
	chronosrt_begin_timed_section(0, &sect);
	while (!atomic_load(&goahead)) {
		asm volatile("");
	}
	chronosrt_end_timed_section(&sect);

	thread_do_work();

	// Exit this thread
	chronosrt_on_exit_thread();
	return NULL;
}

int main(int argc, char **argv) {
	chronos_test_init_runtime(&argc, argv);
	chronos_test_log("Main thread has tid %d", gettid());

	// Spawn test threads
	struct chronosrt_timed_section sect;
	chronosrt_begin_timed_section(0, &sect);
	for (size_t i = 1; i < THREADS_COUNT; ++i) {
		void *new_thread_tcb = chronosrt_new_tcb();
		pthread_t pthread;
		chronos_assert_false(pthread_create(&pthread, NULL, other_thread, new_thread_tcb));
		chronos_assert_false(pthread_detach(pthread));
		chronosrt_on_parent_thread_hook_p(new_thread_tcb, pthread);
	}
	// Give a goahead signal to all simulation threads
	atomic_store(&goahead, 1);
	chronosrt_end_timed_section(&sect);

	thread_do_work();

	// Ensure that other threads complete
	while (atomic_load(&left) != 0) {
		asm volatile("");
	}

	// Exit this thread
	return 0;
}

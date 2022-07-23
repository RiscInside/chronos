#include <pthread.h>
#include <stdatomic.h>

#include <fail.h>
#include <runner.h>
#include <schedpolicy.h>
#include <timeline.h>
#include <unistd.h>

// thread that kills application if value at arg is set
// useful for checking that threads actually get suspended
void *edgy_thread(void *arg) {
	atomic_int *ptr = arg;
	for (;;) {
		if (atomic_load(ptr)) {
			chronosrt_fail("thread didn't get suspended");
		}
	}
	return NULL;
}

int main() {
	struct chronosrt_cfg cfg;
	chronosrt_sched_policy_init(&cfg);

	pthread_t pthread1, pthread2;
	atomic_int var1 = 0, var2 = 0;
	CHRONOSRT_ASSERT_FALSE(pthread_create(&pthread1, NULL, edgy_thread, &var1));
	chronosrt_runner_freezer_init(0);
	chronosrt_runner_freeze_p(pthread1);

	usleep(1000); // at this point it safe to assume that threads have been suspended

	atomic_store(&var1, 1);
	usleep(1000000);

	atomic_store(&var1, 0);
	return EXIT_SUCCESS;
}

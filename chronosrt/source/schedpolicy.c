#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <fail.h>
#include <schedpolicy.h>

#define add_sched_policy(name)                                                                                         \
	static int name##_priority = -1;                                                                                   \
	void chronosrt_sched_policy_set_##name(pid_t tid) {                                                                \
		struct sched_param param;                                                                                      \
		param.sched_priority = name##_priority;                                                                        \
		CHRONOSRT_ASSERT_FALSE(sched_setscheduler(tid, SCHED_FIFO, &param));                                           \
	}                                                                                                                  \
	void chronosrt_sched_policy_set_##name##_p(pthread_t thread) {                                                     \
		struct sched_param param;                                                                                      \
		param.sched_priority = name##_priority;                                                                        \
		CHRONOSRT_ASSERT_FALSE(pthread_setschedparam(thread, SCHED_FIFO, &param));                                     \
	}

add_sched_policy(scheduler);
add_sched_policy(runnable);
add_sched_policy(barrier);
add_sched_policy(suspended);

void chronosrt_sched_policy_init(void) {
	suspended_priority = sched_get_priority_min(SCHED_FIFO);
	barrier_priority = suspended_priority + 1;
	runnable_priority = barrier_priority + 1;
	scheduler_priority = runnable_priority + 1;
	CHRONOSRT_ASSERT_TRUE(scheduler_priority < sched_get_priority_max(SCHED_FIFO));
}

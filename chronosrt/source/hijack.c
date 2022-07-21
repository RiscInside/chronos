#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <limits.h>
#include <pthread.h>
#include <sched.h>
#include <signal.h>
#include <stdbool.h>

#include <fail.h>
#include <hijack.h>

#define HIJACK_HANG(stmt)                                                                                              \
	do {                                                                                                               \
		stmt;                                                                                                          \
	} while (true)

#define HIJACK_HANG_ASM(...) HIJACK_HANG(__asm__ __volatile__(__VA_ARGS__))

void *hijack_freeze_barrier(void *arg) {
	(void)arg;
	// Decrease our priority to allow other tasks on the system to run
	struct sched_param param;
	param.sched_priority = 0;
	sched_setscheduler(gettid(), SCHED_BATCH, &param);
	nice(INT_MAX); // nice out of range is clamped

	// Do nothing. The whole purpose of this thread is to waste CPU time
#if defined(__x86_64) || defined(__i386)
	HIJACK_HANG_ASM("pause");
#else
	HIJACK_HANG("");
#endif
	return NULL;
}

void hijack_init(struct hijack_cpu *info, size_t cpu_id) {
	size_t cpu_alloc_size = CPU_ALLOC_SIZE(cpu_id + 1);
	info->cpuset = CPU_ALLOC(cpu_id + 1);
	CHRONOSRT_ASSERT_TRUE(info->cpuset);
	CPU_ZERO_S(cpu_alloc_size, info->cpuset);
	CPU_SET_S(cpu_id, cpu_alloc_size, info->cpuset);

	CHRONOSRT_ASSERT_FALSE(pthread_create(&info->freezer_barrier, NULL, hijack_freeze_barrier, NULL));
	CHRONOSRT_ASSERT_FALSE(pthread_detach(info->freezer_barrier));
}

void hijack_pin(struct hijack_cpu *info, pid_t pid) {
	CHRONOSRT_ASSERT_FALSE(sched_setaffinity(pid, info->cpu + 1, info->cpuset));
}

void hijack_freeze(struct hijack_cpu *info, pid_t pid) {
	struct sched_param param;
	param.sched_priority = 0;
	CHRONOSRT_ASSERT_FALSE(sched_setscheduler(pid, SCHED_IDLE, &param));
}

void hijack_unfreeze(struct hijack_cpu *info, pid_t pid, int rt_prio) {
	struct sched_param param;
	param.sched_priority = rt_prio;
	CHRONOSRT_ASSERT_TRUE(sched_setscheduler(pid, SCHED_FIFO, &param));
}

void hijack_deinit(struct hijack_cpu *info) {
	CPU_FREE(info->cpuset);
	CHRONOSRT_ASSERT_FALSE(pthread_kill(info->freezer_barrier, SIGKILL));
}

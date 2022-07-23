#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <errno.h>
#include <fail.h>
#include <log.h>
#include <ns.h>

#define NS_IN_S 1000000000

size_t chronosrt_timespec_to_ns(const struct timespec *ts) {
	return ts->tv_sec * NS_IN_S + ts->tv_nsec;
}

void chronosrt_ns_to_timespec(struct timespec *ts, size_t ns) {
	ts->tv_nsec = ns % NS_IN_S;
	ts->tv_sec = ns / NS_IN_S;
}

size_t chronosrt_ns_from_boot(void) {
	struct timespec ts;
	CHRONOSRT_ASSERT_FALSE(clock_gettime(CLOCK_MONOTONIC, &ts));
	return chronosrt_timespec_to_ns(&ts);
}

void chronosrt_sleep(size_t ns) {
	struct timespec timeslice, remaining;
	chronosrt_ns_to_timespec(&timeslice, ns);
	for (;;) {
		if (!nanosleep(&timeslice, &remaining)) {
			break;
		}
		CHRONOSRT_ASSERT_TRUE(errno == EINTR);
		timeslice = remaining;
	}
}

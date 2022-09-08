#include <log.h>
#include <ns.h>
#include <stdlib.h>

void chronos_ns_to_timespec(size_t ns, struct timespec *ts) {
	ts->tv_sec = ns / 1000000000;
	ts->tv_nsec = ns % 1000000000;
}

size_t chronos_timespec_to_ns(struct timespec const *ts) {
	return ts->tv_nsec + ts->tv_sec * 1000000000;
}

size_t chronos_ns_get_clock(clockid_t id) {
	struct timespec ts;
	chronos_assert_false(clock_gettime(id, &ts));
	return chronos_timespec_to_ns(&ts);
}

size_t chronos_ns_get_monotonic(void) {
	return chronos_ns_get_clock(CLOCK_MONOTONIC);
}

size_t chronos_ns_get_thread(void) {
	return chronos_ns_get_clock(CLOCK_THREAD_CPUTIME_ID);
}

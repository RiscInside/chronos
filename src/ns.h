#ifndef __CHRONOS_NS_H__
#define __CHRONOS_NS_H__

#include <stddef.h>
#include <time.h>

#define S_TO_NS(x) (x * 1000000000)
#define MS_TO_NS(x) (x * 1000000)
#define US_TO_NS(x) (x * 1000)

void chronos_ns_to_timespec(size_t ns, struct timespec *ts);
size_t chronos_timespec_to_ns(struct timespec const *ts);
size_t chronos_ns_get_clock(clockid_t id);
size_t chronos_ns_get_monotonic(void);
size_t chronos_ns_get_thread(void);

#endif

#ifndef __CHRONOSRT_NS_H__
#define __CHRONOSRT_NS_H__

#include <time.h>

size_t chronosrt_timespec_to_ns(const struct timespec *ts);

void chronosrt_ns_to_timespec(struct timespec *ts, size_t ns);

size_t chronosrt_ns_from_boot(void);

void chronosrt_sleep(size_t ns);

#endif

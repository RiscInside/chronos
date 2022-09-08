#ifndef __CHRONOS_LOG_H_INCLUDED__
#define __CHRONOS_LOG_H_INCLUDED__

#include <ns.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

bool chronos_is_rt_logging_enabled(void);
bool chronos_is_test_logging_enabled(void);

#define chronos_raw_log(fmt, ...)                                                                                      \
	do {                                                                                                               \
		dprintf(2,                                                                                                     \
		        "["__FILE_NAME__                                                                                       \
		        ":%d (at %zu ns)] " fmt "\n",                                                                          \
		        __LINE__, chronos_ns_get_monotonic() __VA_OPT__(, ) __VA_ARGS__);                                      \
	} while (0);

#define chronos_test_log(fmt, ...)                                                                                     \
	do {                                                                                                               \
		if (chronos_is_test_logging_enabled()) {                                                                       \
			dprintf(2,                                                                                                 \
			        "["__FILE_NAME__                                                                                   \
			        ":%d (at %zu ns)] " fmt "\n",                                                                      \
			        __LINE__, chronos_ns_get_monotonic() __VA_OPT__(, ) __VA_ARGS__);                                  \
		}                                                                                                              \
	} while (0);

#define chronos_rt_log(fmt, ...)                                                                                       \
	do {                                                                                                               \
		if (chronos_is_rt_logging_enabled()) {                                                                         \
			dprintf(2,                                                                                                 \
			        "["__FILE_NAME__                                                                                   \
			        ":%d (at %zu ns)] " fmt "\n",                                                                      \
			        __LINE__, chronos_ns_get_monotonic() __VA_OPT__(, ) __VA_ARGS__);                                  \
		}                                                                                                              \
	} while (0);

#define chronos_assert_false(...)                                                                                      \
	do {                                                                                                               \
		if (__VA_ARGS__) {                                                                                             \
			chronos_raw_log("Assertion failed");                                                                       \
			abort();                                                                                                   \
		}                                                                                                              \
	} while (0);

#define chronos_assert(...)                                                                                            \
	do {                                                                                                               \
		if (!(__VA_ARGS__)) {                                                                                          \
			chronos_raw_log("Assertion failed");                                                                       \
			abort();                                                                                                   \
		}                                                                                                              \
	} while (0);

#endif

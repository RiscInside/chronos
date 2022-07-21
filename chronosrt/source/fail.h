#ifndef __CHRONOSRT_FAIL_H__
#define __CHRONOSRT_FAIL_H__

#include <stdnoreturn.h>

// "fail.h" - error reporting
// chronosrt is intended to be used in instrumentation settings. As such, there is no way to gracefully handle runtime
// errors. This header provides a glorified abort() called fail() that prints a stacktrace

noreturn void chronosrt_fail(const char *fmt, ...);

#define CHRONOSRT_ASSERT_TRUE(expr)                                                                                    \
	do {                                                                                                               \
		if (!expr) {                                                                                                   \
			chronosrt_fail("CHRONOSRT_IS_ZERO check failed (%s:%s)", __FILE__, __LINE__);                              \
		}                                                                                                              \
	} while (0)

#define CHRONOSRT_ASSERT_FALSE(expr)                                                                                   \
	do {                                                                                                               \
		if (expr) {                                                                                                    \
			chronosrt_fail("CHRONOSRT_IS_NOT_ZERO check failed (%s:%s)", __FILE__, __LINE__);                          \
		}                                                                                                              \
	} while (0)

#endif

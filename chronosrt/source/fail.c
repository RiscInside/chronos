#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <errno.h>
#include <execinfo.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <fail.h>

#define BACKTRACE_MAX 20

noreturn void fail(const char *fmt, ...) {
	va_list args;
	va_start(args, fmt);

	int errno_value = errno;

	dprintf(STDERR_FILENO, "[tid %d] ", gettid());
	vdprintf(STDERR_FILENO, fmt, args);
	dprintf(STDERR_FILENO, "\nerrno value (might be irrelevant): %d (%s)\nbacktrace:\n", errno_value,
	        strerror(errno_value));

	void *buffer[BACKTRACE_MAX];

	int size = backtrace(buffer, BACKTRACE_MAX);
	backtrace_symbols_fd(buffer, size, STDERR_FILENO);

	dprintf(STDERR_FILENO, "end of backtrace\n");

	va_end(args);
	abort();
}

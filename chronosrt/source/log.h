#ifndef __CHRONOSRT_LOG_H__
#define __CHRONOSRT_LOG_H__

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <ns.h>
#include <sched.h>
#include <stdio.h>
#include <unistd.h>

#ifdef CHRONOSRT_LOG_TO_STDERR
#if defined(__FILE_NAME__)
#define CHRONOSRT_LOG(fmt, ...)                                                                                        \
	dprintf(STDERR_FILENO, "[thread %d at %zu ns, %s:%d] " fmt "\n", gettid(), chronosrt_ns_from_boot(),               \
	        __FILE_NAME__, __LINE__ __VA_OPT__(, ) __VA_ARGS__)
#else
#define CHRONOSRT_LOG(fmt, ...)                                                                                        \
	dprintf(STDERR_FILENO, "[thread %d at %zu ns, %s:%d] " fmt "\n", gettid(), chronosrt_ns_from_boot(), __FILE__,     \
	        __LINE__ __VA_OPT__(, ) __VA_ARGS__)
#endif
#else
#define CHRONOSRT_LOG(fmt, ...)                                                                                        \
	do {                                                                                                               \
	} while (0)
#endif
#endif

#pragma once

// "timeline.h" - helpers to module various timelines
#include <stdatomic.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

// Waste CPU time. On my PC one iteration takes roughly one millisecond, but this of course can vary wildly. We are not
// interested in exact timings anyway, we just need to capture order of events
static void timeline_spin_for(size_t iterations) {
	volatile size_t n;
	for (size_t i = 0; i < iterations * 500000; ++i) {
		n += i * i;
	}
}

// Event counter that keeps track of last event ID
typedef atomic_size_t timeline_event_cnt_t;

// Declare that event with a given ID "happens" now. This routine checks that events "happen" in the order they should
// happen
static void timeline_event_happened(timeline_event_cnt_t *cnt, size_t id) {
	size_t expected_event_id = atomic_fetch_add(cnt, 1);
	if (expected_event_id != id) {
		fprintf(stderr, "timeline error: event %zu out of order\n", id);
		exit(EXIT_FAILURE);
	}
}

#define timeline_timestamp(s)                                                                                          \
	printf("[virtual: %zu ms, real: %zu ms] %s", chronosrt_get_vruntime() / 1000000,                                   \
	       chronosrt_get_realtime() / 1000000, s)

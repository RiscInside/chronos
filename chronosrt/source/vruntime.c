#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <ns.h>
#include <vruntime.h>

void chronosrt_vruntime_set_virtual_ts(struct chronosrt_vruntime *vruntime, size_t ns) {
	chronosrt_vruntime_ts_t ts = atomic_load_explicit(&vruntime->ts, memory_order_relaxed);
	ts.last_vruntime = ns;
	atomic_store_explicit(&vruntime->ts, ts, memory_order_relaxed);
}

void chronosrt_vruntime_set_real_ts(struct chronosrt_vruntime *vruntime, size_t ns) {
	chronosrt_vruntime_ts_t ts = atomic_load_explicit(&vruntime->ts, memory_order_relaxed);
	ts.last_update_ns = ns;
	atomic_store_explicit(&vruntime->ts, ts, memory_order_relaxed);
}

// NOTE: vruntime can be updated concurrently from multiple threads (more specifically, per-cpu scheduler thread and the
// caller thread). We use double compare and swap to ensure that updates don't overwrite each other
void chronosrt_vruntime_update(struct chronosrt_vruntime *vruntime) {
	for (;;) {
		chronosrt_vruntime_ts_t old_ts = atomic_load_explicit(&vruntime->ts, memory_order_relaxed);

		chronosrt_vruntime_ts_t new_ts = old_ts;
		size_t ns_delta = chronosrt_ns_from_boot() - old_ts.last_update_ns;
		size_t vruntime_delta = ns_delta * CHRONOSRT_VRUNTIME_TDF_UNIT / vruntime->tdf;
		new_ts.last_update_ns += ns_delta;
		new_ts.last_vruntime += vruntime_delta;

		if (atomic_compare_exchange_weak_explicit(&vruntime->ts, &old_ts, new_ts, memory_order_relaxed,
		                                          memory_order_relaxed)) {
			break;
		}
	}
}

void chronosrt_vruntime_set_tdf(struct chronosrt_vruntime *vruntime, double tdf) {
	vruntime->tdf = (size_t)(tdf * (double)CHRONOSRT_VRUNTIME_TDF_UNIT);
}

size_t chronosrt_vruntime_get(struct chronosrt_vruntime *vruntime) {
	return atomic_load_explicit(&vruntime->ts, memory_order_relaxed).last_vruntime;
}

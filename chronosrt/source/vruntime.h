#ifndef __CHRONOSRT_VRUNTIME_H__
#define __CHRONOSRT_VRUNTIME_H__

#include <stdatomic.h>
#include <stddef.h>

// Actual tdf is vruntime->tdf / CHRONOSRT_VRUNTIME_TDF_UNIT
#define CHRONOSRT_VRUNTIME_TDF_UNIT 1000000

typedef struct chronosrt_vruntime_ts_struct {
	size_t last_update_ns;
	size_t last_vruntime;
} chronosrt_vruntime_ts_t;

struct chronosrt_vruntime {
	_Atomic chronosrt_vruntime_ts_t ts;
	size_t tdf;
};

void chronosrt_vruntime_set_virtual_ts(struct chronosrt_vruntime *vruntime, size_t ns);

void chronosrt_vruntime_set_real_ts(struct chronosrt_vruntime *vruntime, size_t ns);

void chronosrt_vruntime_update(struct chronosrt_vruntime *vruntime);

void chronosrt_vruntime_set_tdf(struct chronosrt_vruntime *vruntime, double tdf);

size_t chronosrt_vruntime_get(struct chronosrt_vruntime *vruntime);

#endif

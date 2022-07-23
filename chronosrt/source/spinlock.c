#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <fail.h>
#include <spinlock.h>
#include <stdlib.h>

#define THREADS_PTR(spinlock) (spinlock)
#define LAST_CELL_PTR(spinlock) (spinlock + 1)
#define CELL_PTR(spinlock, n) ((spinlock) + 2 + (n))

chronosrt_spinlock_t chronosrt_new_spinlock(size_t max_threads) {
	chronosrt_spinlock_t res = calloc(max_threads + 2, sizeof(size_t));
	CHRONOSRT_ASSERT_TRUE(res);
	*THREADS_PTR(res) = max_threads;
	*CELL_PTR(res, 0) = 1;
	return res;
}

void chronosrt_spinlock_destroy(chronosrt_spinlock_t spinlock) {
	free(spinlock);
}

size_t chronosrt_spinlock(chronosrt_spinlock_t lock) {
	size_t threads = *THREADS_PTR(lock);
	size_t token = atomic_fetch_add_explicit(LAST_CELL_PTR(lock), 1, memory_order_acq_rel);
	atomic_size_t *cell = CELL_PTR(lock, token % threads);
	while (!atomic_load_explicit(cell, memory_order_acquire)) {
#if defined(__x86_64)
		__asm__ volatile("pause");
#else
		__asm__ volatile("");
#endif
	}
	return token;
}

void chronosrt_spinunlock(chronosrt_spinlock_t lock, size_t token) {
	size_t threads = *THREADS_PTR(lock);
	atomic_size_t *current_cell = CELL_PTR(lock, token % threads);
	atomic_size_t *next_cell = CELL_PTR(lock, (token + 1) % threads);
	atomic_store_explicit(current_cell, 0, memory_order_relaxed);
	atomic_store_explicit(next_cell, 1, memory_order_release);
}

#ifndef __SPINLOCK_H_INCLUDED__
#define __SPINLOCK_H_INCLUDED__

#include <stdatomic.h>
#include <stddef.h>

typedef atomic_size_t *chronosrt_spinlock_t;

chronosrt_spinlock_t chronosrt_new_spinlock(size_t max_threads);

void chronosrt_spinlock_destroy(chronosrt_spinlock_t spinlock);

size_t chronosrt_spinlock(chronosrt_spinlock_t lock);

void chronosrt_spinunlock(chronosrt_spinlock_t lock, size_t ticket);

#endif

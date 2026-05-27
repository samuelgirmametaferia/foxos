#pragma once
#include <stdint.h>

/*
 * Reader-Writer Spinlock for concurrent data structures like dcache.
 * lock < 0 indicates writer is holding the lock.
 * lock > 0 indicates number of readers holding the lock.
 * lock == 0 indicates unlocked.
 */

typedef struct {
    volatile int32_t lock;
} rwlock_t;

#define RWLOCK_INIT {0}

static inline void rwlock_init(rwlock_t* lock) {
    lock->lock = 0;
}

static inline void rwlock_acquire_read(rwlock_t* lock) {
    while(1) {
        int32_t val = lock->lock;
        if (val >= 0) {
            if (__sync_bool_compare_and_swap(&lock->lock, val, val + 1)) {
                break;
            }
        }
        __asm__ __volatile__("pause");
    }
}

static inline void rwlock_release_read(rwlock_t* lock) {
    __sync_fetch_and_sub(&lock->lock, 1);
}

static inline void rwlock_acquire_write(rwlock_t* lock) {
    while(1) {
        if (__sync_bool_compare_and_swap(&lock->lock, 0, -1)) {
            break;
        }
        __asm__ __volatile__("pause");
    }
}

static inline void rwlock_release_write(rwlock_t* lock) {
    lock->lock = 0;
}

#pragma once
#include <stdint.h>

/*
 * Simple spinlock implementation for multicore synchronization.
 * Uses x86-64 LOCK prefix for atomic operations.
 */

typedef struct {
    volatile uint8_t locked;
} __attribute__((packed)) spinlock_t;

#define SPINLOCK_INIT {0}

/* Acquire spinlock (spins until lock acquired) */
static inline void spinlock_acquire(spinlock_t* lock) {
    /* Note: In a real kernel, we should disable interrupts here to prevent deadlocks 
       if an interrupt handler tries to acquire the same lock. */
    while (1) {
        uint8_t expected = 0;
        __asm__ __volatile__(
            "lock cmpxchgb %2, %0"
            : "+m"(lock->locked), "+a"(expected)
            : "r"((uint8_t)1)
            : "cc", "memory"
        );
        if (expected == 0) break;
        __asm__ __volatile__("pause");
    }
}

/* Release spinlock */
static inline void spinlock_release(spinlock_t* lock) {
    __asm__ __volatile__(
        "movb $0, %0"
        : "+m"(lock->locked)
        : 
        : "memory"
    );
}

/* Enhanced IRQ-safe spinlock functions */
static inline uint64_t spinlock_acquire_irqsave(spinlock_t* lock) {
    uint64_t rflags;
    __asm__ __volatile__("pushfq; pop %0; cli" : "=r"(rflags) :: "memory");
    spinlock_acquire(lock);
    return rflags;
}

static inline void spinlock_release_irqrestore(spinlock_t* lock, uint64_t rflags) {
    spinlock_release(lock);
    __asm__ __volatile__("push %0; popfq" :: "r"(rflags) : "memory", "cc");
}

/* Try to acquire spinlock (non-blocking) */
static inline int spinlock_try_acquire(spinlock_t* lock) {
    uint8_t expected = 0;
    __asm__ __volatile__(
        "lock cmpxchgb %2, %0"
        : "+m"(lock->locked), "=a"(expected)
        : "r"((uint8_t)1), "1"(expected)
        : "cc", "memory"
    );
    return expected == 0;
}

/* Check if lock is held */
static inline int spinlock_is_held(spinlock_t* lock) {
    return lock->locked != 0;
}

#pragma once
#include <stdint.h>
#include "sched.h"

/*
 * I/O Waiting Infrastructure
 * 
 * Provides scheduler-aware blocking primitives that allow threads to yield
 * while waiting for I/O completion. When an I/O device (keyboard, disk, etc)
 * completes, it wakes the waiting thread via scheduler.
 * 
 * This allows the kernel to realize multicore efficiency gains by allowing
 * other threads to run while one thread waits for I/O.
 */

// Wait queue for threads waiting on I/O events
typedef struct {
    int thread_id;
    int active;  // 1 = thread is waiting, 0 = not waiting
} wait_entry_t;

typedef struct {
    wait_entry_t entries[MAX_THREADS];
    int count;
} wait_queue_t;

// Initialize a wait queue
void io_wait_queue_init(wait_queue_t* queue);

// Block current thread on a wait queue
// Returns when another thread calls io_wait_wake()
int io_wait_on_queue(wait_queue_t* queue);

// Wake all threads waiting on a queue (typically called from interrupt handler)
void io_wait_wake_all(wait_queue_t* queue);

// Wake first waiting thread on a queue
void io_wait_wake_one(wait_queue_t* queue);

// Scheduler-aware sleep that yields to other threads
// (better than busy-wait loop in timer_sleep)
void io_sleep_blocking(uint64_t ms);

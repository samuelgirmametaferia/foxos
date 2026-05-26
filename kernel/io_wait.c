#include "io_wait.h"
#include "sched.h"
#include "timer.h"

void io_wait_queue_init(wait_queue_t* queue) {
    if (!queue) return;
    queue->count = 0;
    for (int i = 0; i < MAX_THREADS; i++) {
        queue->entries[i].thread_id = -1;
        queue->entries[i].active = 0;
    }
}

int io_wait_on_queue(wait_queue_t* queue) {
    if (!queue || queue->count >= MAX_THREADS) return -1;
    
    // Get current thread ID
    int tid = scheduler_current_thread_id();
    
    // Find free slot in queue
    for (int i = 0; i < MAX_THREADS; i++) {
        if (!queue->entries[i].active) {
            queue->entries[i].thread_id = tid;
            queue->entries[i].active = 1;
            queue->count++;
            
            // Mark thread as BLOCKED in scheduler
            scheduler_block_current();
            
            // Wait until another thread calls io_wait_wake_*
            // The interrupt handler will wake us via io_wait_wake_one/all
            while (queue->entries[i].active) {
                __asm__ __volatile__("hlt");
            }
            
            return 0;
        }
    }
    
    return -1;
}

void io_wait_wake_all(wait_queue_t* queue) {
    if (!queue) return;
    
    for (int i = 0; i < MAX_THREADS; i++) {
        if (queue->entries[i].active) {
            queue->entries[i].active = 0;
            scheduler_unblock_thread(queue->entries[i].thread_id);
        }
    }
    queue->count = 0;
}

void io_wait_wake_one(wait_queue_t* queue) {
    if (!queue || queue->count == 0) return;
    
    // Wake the first waiting thread
    for (int i = 0; i < MAX_THREADS; i++) {
        if (queue->entries[i].active) {
            queue->entries[i].active = 0;
            scheduler_unblock_thread(queue->entries[i].thread_id);
            queue->count--;
            return;
        }
    }
}

void io_sleep_blocking(uint64_t ms) {
    // Use scheduler sleep instead of busy-wait
    // This yields to other threads while sleeping
    int tid = scheduler_current_thread_id();
    scheduler_thread_sleep(tid, ms);
}

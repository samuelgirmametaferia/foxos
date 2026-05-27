#pragma once
#include <stdint.h>
#include "idt.h"

#define MAX_THREADS 64

/* Thread states */
typedef enum {
    THREAD_READY = 0,
    THREAD_RUNNING = 1,
    THREAD_BLOCKED = 2,
    THREAD_SLEEPING = 3,
    THREAD_IDLE = 4
} thread_state_t;

void scheduler_init(void);
int scheduler_create(void (*entry)(void));
void scheduler_set_idle(void (*entry)(void));
void scheduler_start(void);
void scheduler_stop(void);
registers_t* scheduler_tick(registers_t* regs);

/* New APIs for enhanced scheduling */
int scheduler_get_thread_count(void);
int scheduler_set_thread_priority(int thread_id, uint8_t priority);
void scheduler_thread_sleep(int thread_id, uint64_t ms);
void scheduler_thread_wake(int thread_id);
thread_state_t scheduler_get_thread_state(int thread_id);
uint32_t scheduler_get_cpu_id(void);
void scheduler_ap_start(void);
void scheduler_yield(void);

/* I/O blocking support */
int scheduler_current_thread_id(void);
int scheduler_block_current(void);
int scheduler_unblock_thread(int thread_id);

int scheduler_create_user(void (*entry)(void), uint64_t user_rsp, uint32_t pid);
uint32_t scheduler_get_thread_pid(int thread_id);
void scheduler_set_thread_pid(int thread_id, uint32_t pid);
void scheduler_set_thread_state_idle(int thread_id);

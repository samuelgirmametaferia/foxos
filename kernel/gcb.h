#pragma once
#include <stdint.h>
#include <stdatomic.h>

#define GCB_MAGIC UINT64_C(0x3142434730584F46) /* FOX0GCB1 */

typedef struct gcb_t {
    uint64_t magic;
    _Atomic uint64_t panic_flag;
    void* scheduler_state;
    _Atomic uint64_t global_ticks;
    uint32_t cpu_count;
    _Atomic uint32_t log_head;
    _Atomic uint32_t log_tail;
    uint8_t _pad0[16];
    uint8_t current_log[1008];
} __attribute__((aligned(64))) gcb_t;

extern gcb_t GCB;

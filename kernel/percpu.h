#pragma once
#include <stdint.h>

/*
 * Per-CPU data structures for multicore support.
 * Uses GS base for per-CPU area pointer.
 */

typedef struct {
    uint32_t cpu_id;
    uint32_t apic_id;
    void* local_tss;
    void* idle_thread;
    uint64_t context_switches;
    uint64_t interrupts_handled;
} __attribute__((packed)) percpu_t;

/* Get current CPU's per-CPU structure */
static inline percpu_t* percpu_get_current(void) {
    percpu_t* ptr = 0;
    __asm__ __volatile__("mov %%gs:0, %0" : "=r"(ptr));
    return ptr;
}

/* Get specific CPU's per-CPU structure */
percpu_t* percpu_get(uint32_t cpu_id);

/* Initialize per-CPU area for BSP (main CPU) */
void percpu_init_bsp(void);

/* Initialize per-CPU area for AP (application processor) */
void percpu_init_ap(uint32_t cpu_id, uint32_t apic_id);

/* Total number of CPUs online */
uint32_t percpu_cpu_count(void);

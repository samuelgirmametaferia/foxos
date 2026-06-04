#include "percpu.h"
#include "memory.h"
#include "serial.h"

#define MAX_CPUS 16

/* Use static allocation instead of kmalloc to avoid initialization order issues */
static percpu_t percpu_areas_static[MAX_CPUS];
static percpu_t* percpu_areas[MAX_CPUS];
static uint32_t online_cpu_count = 1;

void percpu_init_bsp(void) {
    /* Use pre-allocated BSP per-CPU area */
    percpu_t* area = &percpu_areas_static[0];
    
    area->self = area;
    area->cpu_id = 0;
    area->apic_id = 0;
    area->kernel_stack = 0;
    area->scratch_rsp = 0;
    area->local_tss = 0;
    area->idle_thread = 0;
    area->context_switches = 0;
    area->interrupts_handled = 0;
    
    percpu_areas[0] = area;
    online_cpu_count = 1;
    
    /* Set GS base and KERNEL_GS_BASE to point to BSP per-CPU area.
       GS_BASE (0xC0000101) is used while in kernel.
       KERNEL_GS_BASE (0xC0000102) is used while in user (swapgs switches them). */
    uint64_t addr = (uint64_t)(uintptr_t)area;
    uint32_t low = addr & 0xFFFFFFFF;
    uint32_t high = addr >> 32;

    __asm__ __volatile__("wrmsr" : : "c"(0xC0000101), "a"(low), "d"(high));
    __asm__ __volatile__("wrmsr" : : "c"(0xC0000102), "a"(low), "d"(high));
    
    serial_writeln("[percpu] BSP initialized");
}

void percpu_init_ap(uint32_t cpu_id, uint32_t apic_id) {
    if (cpu_id >= MAX_CPUS) {
        serial_writeln("[percpu] CPU ID out of range");
        return;
    }
    
    percpu_t* area = &percpu_areas_static[cpu_id];
    
    area->self = area;
    area->cpu_id = cpu_id;
    area->apic_id = apic_id;
    area->kernel_stack = 0;
    area->scratch_rsp = 0;
    area->local_tss = 0;
    area->idle_thread = 0;
    area->context_switches = 0;
    area->interrupts_handled = 0;
    
    percpu_areas[cpu_id] = area;
    online_cpu_count = cpu_id + 1;
    
    /* Set GS base and KERNEL_GS_BASE for this CPU */
    uint64_t addr = (uint64_t)(uintptr_t)area;
    uint32_t low = addr & 0xFFFFFFFF;
    uint32_t high = addr >> 32;

    __asm__ __volatile__("wrmsr" : : "c"(0xC0000101), "a"(low), "d"(high));
    __asm__ __volatile__("wrmsr" : : "c"(0xC0000102), "a"(low), "d"(high));
}

percpu_t* percpu_get(uint32_t cpu_id) {
    if (cpu_id >= MAX_CPUS) return 0;
    return percpu_areas[cpu_id];
}

uint32_t percpu_cpu_count(void) {
    return online_cpu_count;
}

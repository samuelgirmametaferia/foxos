#include "gdt.h"
#include "smp.h"
#include "memory.h"
#include "percpu.h"

typedef struct {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t base_middle;
    uint8_t access;
    uint8_t granularity;
    uint8_t base_high;
} __attribute__((packed)) gdt_entry_t;

typedef struct {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t base_middle;
    uint8_t access;
    uint8_t granularity;
    uint8_t base_high;
    uint32_t base_upper;
    uint32_t reserved;
} __attribute__((packed)) tss_entry_t;

typedef struct {
    uint32_t reserved0;
    uint64_t rsp0;
    uint64_t rsp1;
    uint64_t rsp2;
    uint64_t reserved1;
    uint64_t ist1;
    uint64_t ist2;
    uint64_t ist3;
    uint64_t ist4;
    uint64_t ist5;
    uint64_t ist6;
    uint64_t ist7;
    uint64_t reserved2;
    uint16_t reserved3;
    uint16_t iopb_offset;
} __attribute__((packed)) tss_t;

typedef struct {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed)) gdt_ptr_t;

// Per-CPU GDTs and TSSs
// Max CPUs supported by our percpu architecture is 16
#define MAX_CPUS 16

// 0: Null, 1: KCode, 2: KData, 3: UData, 4: UCode, 5: TSS (2 entries)
#define GDT_ENTRIES 7

static gdt_entry_t gdts[MAX_CPUS][GDT_ENTRIES];
static tss_t tsses[MAX_CPUS];
static gdt_ptr_t gdt_ptrs[MAX_CPUS];

static void set_gdt_entry(int cpu, int index, uint32_t base, uint32_t limit, uint8_t access, uint8_t gran) {
    gdts[cpu][index].base_low = (base & 0xFFFF);
    gdts[cpu][index].base_middle = (base >> 16) & 0xFF;
    gdts[cpu][index].base_high = (base >> 24) & 0xFF;
    gdts[cpu][index].limit_low = (limit & 0xFFFF);
    gdts[cpu][index].granularity = ((limit >> 16) & 0x0F) | (gran & 0xF0);
    gdts[cpu][index].access = access;
}

static void set_tss_entry(int cpu, int index, uint64_t base, uint32_t limit, uint8_t access, uint8_t gran) {
    tss_entry_t* tss_desc = (tss_entry_t*)&gdts[cpu][index];
    tss_desc->base_low = (base & 0xFFFF);
    tss_desc->base_middle = (base >> 16) & 0xFF;
    tss_desc->base_high = (base >> 24) & 0xFF;
    tss_desc->base_upper = (base >> 32) & 0xFFFFFFFF;
    tss_desc->limit_low = (limit & 0xFFFF);
    tss_desc->granularity = ((limit >> 16) & 0x0F) | (gran & 0xF0);
    tss_desc->access = access;
    tss_desc->reserved = 0;
}

static void init_cpu_gdt(int cpu) {
    uint64_t tss_base = (uint64_t)&tsses[cpu];
    uint32_t tss_limit = sizeof(tss_t) - 1;

    tsses[cpu].iopb_offset = sizeof(tss_t);

    set_gdt_entry(cpu, 0, 0, 0, 0, 0);                // Null
    set_gdt_entry(cpu, 1, 0, 0xFFFFF, 0x9A, 0x20); // Kernel Code (64-bit) (0x20 = L bit)
    set_gdt_entry(cpu, 2, 0, 0xFFFFF, 0x92, 0x00); // Kernel Data
    set_gdt_entry(cpu, 3, 0, 0xFFFFF, 0xF2, 0x00); // User Data (DPL 3)
    set_gdt_entry(cpu, 4, 0, 0xFFFFF, 0xFA, 0x20); // User Code (64-bit, DPL 3)
    
    set_tss_entry(cpu, 5, tss_base, tss_limit, 0x89, 0x00); // TSS (takes index 5 and 6)

    gdt_ptrs[cpu].limit = sizeof(gdt_entry_t) * GDT_ENTRIES - 1;
    gdt_ptrs[cpu].base = (uint64_t)&gdts[cpu][0];
}

extern void load_gdt(uint64_t ptr);
extern void load_tss(void);

void gdt_init(void) {
    for (int i = 0; i < MAX_CPUS; i++) {
        init_cpu_gdt(i);
    }
    __asm__ __volatile__("lgdt %0" : : "m"(gdt_ptrs[0]));
    
    __asm__ __volatile__(
        "pushq $0x08\n\t"
        "leaq .reload_cs_gdt(%%rip), %%rax\n\t"
        "pushq %%rax\n\t"
        "lretq\n\t"
        ".reload_cs_gdt:\n\t"
        "mov $0x10, %%ax\n\t"
        "mov %%ax, %%ds\n\t"
        "mov %%ax, %%es\n\t"
        "mov %%ax, %%fs\n\t"
        "mov %%ax, %%gs\n\t"
        "mov %%ax, %%ss\n\t"
        : : : "rax", "memory"
    );

    __asm__ __volatile__("mov $0x2B, %%ax; ltr %%ax" : : : "ax"); // 5 * 8 = 40 = 0x28 + 3 (RPL) = 0x2B
}

void gdt_init_ap(void) {
    int cpu = percpu_get_current()->cpu_id;
    __asm__ __volatile__("lgdt %0" : : "m"(gdt_ptrs[cpu]));
    
    __asm__ __volatile__(
        "pushq $0x08\n\t"
        "leaq .reload_cs_gdt_ap(%%rip), %%rax\n\t"
        "pushq %%rax\n\t"
        "lretq\n\t"
        ".reload_cs_gdt_ap:\n\t"
        "mov $0x10, %%ax\n\t"
        "mov %%ax, %%ds\n\t"
        "mov %%ax, %%es\n\t"
        "mov %%ax, %%fs\n\t"
        "mov %%ax, %%gs\n\t"
        "mov %%ax, %%ss\n\t"
        : : : "rax", "memory"
    );

    __asm__ __volatile__("mov $0x2B, %%ax; ltr %%ax" : : : "ax");
}

void tss_set_rsp0(uint64_t rsp0) {
    int cpu = percpu_get_current()->cpu_id;
    tsses[cpu].rsp0 = rsp0;
}

#include "syscall.h"
#include "memory.h"
#include "serial.h"
#include "console.h"
#include "vfs.h"
#include "process.h"
#include "sched.h"
#include "timer.h"
#include "../common/lib.h"

#define MSR_EFER   0xC0000080
#define MSR_STAR   0xC0000081
#define MSR_LSTAR  0xC0000082
#define MSR_FMASK  0xC0000084

static inline void wrmsr(uint32_t msr, uint64_t val) {
    uint32_t low = val & 0xFFFFFFFF;
    uint32_t high = val >> 32;
    __asm__ __volatile__("wrmsr" : : "c"(msr), "a"(low), "d"(high));
}

static inline uint64_t rdmsr(uint32_t msr) {
    uint32_t low, high;
    __asm__ __volatile__("rdmsr" : "=a"(low), "=d"(high) : "c"(msr));
    return ((uint64_t)high << 32) | low;
}

extern void syscall_entry(void);

void syscall_init(void) {
    uint64_t efer = rdmsr(MSR_EFER);
    wrmsr(MSR_EFER, efer | 0x801); // NXE and SCE

    uint64_t star = ((uint64_t)0x10 << 48) | ((uint64_t)0x08 << 32);
    wrmsr(MSR_STAR, star);
    wrmsr(MSR_LSTAR, (uint64_t)syscall_entry);
    wrmsr(MSR_FMASK, 0x600);
}

void syscall_init_ap(void) {
    uint64_t efer = rdmsr(MSR_EFER);
    wrmsr(MSR_EFER, efer | 0x801); // NXE and SCE
    uint64_t star = ((uint64_t)0x10 << 48) | ((uint64_t)0x08 << 32);
    wrmsr(MSR_STAR, star);
    wrmsr(MSR_LSTAR, (uint64_t)syscall_entry);
    wrmsr(MSR_FMASK, 0x600);
}

extern uint64_t rust_syscall_handler(uint64_t rdi, uint64_t rsi, uint64_t rdx, uint64_t rcx, uint64_t r8, uint64_t r9, uint64_t sys_num);

uint64_t syscall_handler(uint64_t rdi, uint64_t rsi, uint64_t rdx, uint64_t rcx, uint64_t r8, uint64_t r9, uint64_t sys_num) {
    /* SYSCALL masks interrupts, re-enable them so functions like timer_sleep work */
    __asm__ __volatile__("sti");

    uint64_t ret = rust_syscall_handler(rdi, rsi, rdx, rcx, r8, r9, sys_num);

    __asm__ __volatile__("cli");
    return ret;
}

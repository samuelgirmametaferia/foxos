#include "sched.h"
#include "memory.h"

#define MAX_THREADS 8
#define THREAD_STACK_SIZE (16u * 1024u)

typedef struct {
    uint64_t* rsp;
    uint8_t active;
} thread_t;

static thread_t threads[MAX_THREADS];
static int thread_count = 0;
static int current_idx = 0;
static int sched_enabled = 0;
static uint16_t kernel_cs = 0;
static uint64_t kernel_rflags = 0;
static void (*idle_entry)(void) = 0;
static int idle_idx = -1;

static void memzero(void* ptr, uint32_t len) {
    uint8_t* p = (uint8_t*)ptr;
    for (uint32_t i = 0; i < len; ++i) p[i] = 0;
}

void scheduler_init(void) {
    __asm__ __volatile__("mov %%cs, %0" : "=r" (kernel_cs));
    __asm__ __volatile__("pushfq\n\tpop %0" : "=r" (kernel_rflags));
    for (int i = 0; i < MAX_THREADS; ++i) {
        threads[i].rsp = 0;
        threads[i].active = 0;
    }
    thread_count = 1;
    current_idx = 0;
    threads[0].active = 1; /* current (kernel_main) */
    sched_enabled = 0;
    idle_entry = 0;
    idle_idx = -1;
}

void scheduler_set_idle(void (*entry)(void)) {
    idle_entry = entry;
}

static int scheduler_create_from_template(void (*entry)(void), registers_t* tpl) {
    if (!entry || !tpl) return -1;
    if (thread_count >= MAX_THREADS) return -2;
    uint8_t* stack = (uint8_t*)kmalloc(THREAD_STACK_SIZE);
    if (!stack) return -3;

    uintptr_t top = (uintptr_t)stack + THREAD_STACK_SIZE;
    top &= ~0xFUL;
    top -= sizeof(registers_t);
    registers_t* frame = (registers_t*)top;
    *frame = *tpl;
    frame->rip = (uint64_t)(uintptr_t)entry;
    frame->int_no = 32;
    frame->err_code = 0;
    frame->rflags = (tpl->rflags | 0x200) | 0x2;

    threads[thread_count].rsp = (uint64_t*)frame;
    threads[thread_count].active = 1;
    return thread_count++;
}

int scheduler_create(void (*entry)(void)) {
    if (!entry) return -1;
    if (thread_count >= MAX_THREADS) return -2;

    uint8_t* stack = (uint8_t*)kmalloc(THREAD_STACK_SIZE);
    if (!stack) return -3;

    uintptr_t top = (uintptr_t)stack + THREAD_STACK_SIZE;
    top &= ~0xFUL;
    top -= sizeof(registers_t);
    registers_t* frame = (registers_t*)top;
    memzero(frame, sizeof(registers_t));
    frame->rip = (uint64_t)(uintptr_t)entry;
    frame->cs = (uint64_t)kernel_cs;
    frame->rflags = (kernel_rflags | 0x200) | 0x2;
    frame->int_no = 32;
    frame->err_code = 0;

    threads[thread_count].rsp = (uint64_t*)frame;
    threads[thread_count].active = 1;
    return thread_count++;
}

void scheduler_start(void) {
    sched_enabled = 1;
}

void scheduler_stop(void) {
    sched_enabled = 0;
}

registers_t* scheduler_tick(registers_t* regs) {
    if (idle_idx < 0 && idle_entry) {
        int idx = scheduler_create_from_template(idle_entry, regs);
        if (idx >= 0) idle_idx = idx;
    }

    if (!sched_enabled || thread_count < 2) return regs;

    threads[current_idx].rsp = (uint64_t*)regs;

    int next = current_idx;
    for (int i = 0; i < thread_count; ++i) {
        int idx = (current_idx + 1 + i) % thread_count;
        if (threads[idx].active && threads[idx].rsp) { next = idx; break; }
    }

    if (next == current_idx) return regs;
    current_idx = next;
    return (registers_t*)threads[next].rsp;
}

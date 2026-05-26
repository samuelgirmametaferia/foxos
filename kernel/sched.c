#include "sched.h"
#include "memory.h"
#include "serial.h"

#define MAX_THREADS 64
#define THREAD_STACK_SIZE (16u * 1024u)

typedef struct {
    uint64_t* rsp;
    thread_state_t state;
    uint8_t priority;
    uint64_t sleep_until_ticks;
    uint32_t cpu_affinity;
} thread_t;

static thread_t threads[MAX_THREADS];
static int thread_count = 0;
static int current_idx = 0;
static int sched_enabled = 0;
static uint16_t kernel_cs = 0;
static uint64_t kernel_rflags = 0;
static void (*idle_entry)(void) = 0;
static int idle_idx = -1;
static uint64_t scheduler_ticks = 0;

static void memzero(void* ptr, uint32_t len) {
    uint8_t* p = (uint8_t*)ptr;
    for (uint32_t i = 0; i < len; ++i) p[i] = 0;
}

void scheduler_init(void) {
    __asm__ __volatile__("mov %%cs, %0" : "=r" (kernel_cs));
    __asm__ __volatile__("pushfq\n\tpop %0" : "=r" (kernel_rflags));
    for (int i = 0; i < MAX_THREADS; ++i) {
        threads[i].rsp = 0;
        threads[i].state = THREAD_READY;
        threads[i].priority = 128; /* normal priority */
        threads[i].sleep_until_ticks = 0;
        threads[i].cpu_affinity = 0;
    }
    thread_count = 1;
    current_idx = 0;
    threads[0].state = THREAD_RUNNING;
    threads[0].priority = 128;
    sched_enabled = 0;
    idle_entry = 0;
    idle_idx = -1;
    scheduler_ticks = 0;
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
    frame->cs = tpl->cs;
    frame->int_no = 32;
    frame->err_code = 0;
    frame->rflags = (tpl->rflags | 0x200) | 0x2;

    threads[thread_count].rsp = (uint64_t*)frame;
    threads[thread_count].state = THREAD_READY;
    threads[thread_count].priority = 128;
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
    threads[thread_count].state = THREAD_READY;
    threads[thread_count].priority = 128;
    return thread_count++;
}

void scheduler_start(void) {
    sched_enabled = 1;
}

void scheduler_stop(void) {
    sched_enabled = 0;
}

int scheduler_get_thread_count(void) {
    return thread_count;
}

int scheduler_set_thread_priority(int thread_id, uint8_t priority) {
    if (thread_id < 0 || thread_id >= thread_count) return -1;
    threads[thread_id].priority = priority;
    return 0;
}

void scheduler_thread_sleep(int thread_id, uint64_t ms) {
    if (thread_id < 0 || thread_id >= thread_count) return;
    threads[thread_id].state = THREAD_SLEEPING;
    uint64_t ticks_to_wait = (ms * 100) / 1000u;
    if (ticks_to_wait == 0 && ms > 0) ticks_to_wait = 1;
    threads[thread_id].sleep_until_ticks = scheduler_ticks + ticks_to_wait;
}

void scheduler_thread_wake(int thread_id) {
    if (thread_id < 0 || thread_id >= thread_count) return;
    if (threads[thread_id].state == THREAD_SLEEPING) {
        threads[thread_id].state = THREAD_READY;
    }
}

thread_state_t scheduler_get_thread_state(int thread_id) {
    if (thread_id < 0 || thread_id >= thread_count) return -1;
    return threads[thread_id].state;
}

uint32_t scheduler_get_cpu_id(void) {
    return 0; /* TODO: Return actual CPU ID from per-CPU area */
}

int scheduler_current_thread_id(void) {
    return current_idx;
}

int scheduler_block_current(void) {
    if (current_idx < 0 || current_idx >= thread_count) return -1;
    threads[current_idx].state = THREAD_BLOCKED;
    return 0;
}

int scheduler_unblock_thread(int thread_id) {
    if (thread_id < 0 || thread_id >= thread_count) return -1;
    if (threads[thread_id].state == THREAD_BLOCKED) {
        threads[thread_id].state = THREAD_READY;
    }
    return 0;
}

registers_t* scheduler_tick(registers_t* regs) {
    scheduler_ticks++;
    
    /* Initialize main thread's RSP on first interrupt if not done */
    if (threads[0].rsp == 0) {
        threads[0].rsp = (uint64_t*)regs;
    }
    
    if (idle_idx < 0 && idle_entry) {
        int idx = scheduler_create_from_template(idle_entry, regs);
        if (idx >= 0) {
            idle_idx = idx;
            threads[idx].state = THREAD_IDLE;
        }
    }

    /* Check for threads that should wake up */
    for (int i = 0; i < thread_count; ++i) {
        if (threads[i].state == THREAD_SLEEPING && 
            scheduler_ticks >= threads[i].sleep_until_ticks) {
            threads[i].state = THREAD_READY;
        }
    }

    /* SAFETY FIX: Do NOT switch contexts during interrupts.
     * The current idt_stubs.asm architecture cannot safely handle context
     * switching via iretq because it treats the return value as a stack pointer.
     * Context switching should happen at explicit yield points instead.
     */
    
    if (!sched_enabled || thread_count < 2) return regs;

    /* Save current thread's registers for future reference */
    threads[current_idx].rsp = (uint64_t*)regs;
    threads[current_idx].state = THREAD_READY;

    /* Always return current context - do NOT switch now */
    return regs;
}

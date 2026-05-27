#include "sched.h"
#include "memory.h"
#include "serial.h"
#include "spinlock.h"
#include "percpu.h"
#include "apic.h"
#include "smp.h"

#define MAX_THREADS 64
#define THREAD_STACK_SIZE (16u * 1024u)
#define MAX_CPUS 16

typedef struct {
    uint64_t* rsp;
    uint64_t rsp_top;
    uint32_t pid;
    thread_state_t state;
    uint8_t priority;
    uint64_t sleep_until_ticks;
} thread_t;

static thread_t threads[MAX_THREADS];
static uint32_t thread_cpu_affinity[MAX_THREADS];
static int thread_count = 0;
static spinlock_t thread_alloc_lock = SPINLOCK_INIT;

typedef struct {
    spinlock_t lock;
    int current_idx;
    int idle_idx;
    uint64_t ticks;
} cpu_runqueue_t;

static cpu_runqueue_t runqueues[MAX_CPUS];
static int sched_enabled = 0;
static uint16_t kernel_cs = 0;
static uint64_t kernel_rflags = 0;
static void (*idle_entry)(void) = 0;

extern void context_restore(void* stack_ptr);

void scheduler_init(void) {
    __asm__ __volatile__("mov %%cs, %0" : "=r" (kernel_cs));
    __asm__ __volatile__("pushfq\n\tpop %0" : "=r" (kernel_rflags));
    for (int i = 0; i < MAX_THREADS; ++i) {
        threads[i].rsp = 0;
        threads[i].rsp_top = 0;
        threads[i].pid = 0;
        threads[i].state = THREAD_READY;
        threads[i].priority = 128;
        threads[i].sleep_until_ticks = 0;
        thread_cpu_affinity[i] = 0;
    }
    
    for (int i = 0; i < MAX_CPUS; ++i) {
        runqueues[i].lock.locked = 0;
        runqueues[i].current_idx = 0;
        runqueues[i].idle_idx = -1;
        runqueues[i].ticks = 0;
    }
    
    spinlock_acquire(&thread_alloc_lock);
    thread_count = 1;
    threads[0].state = THREAD_RUNNING;
    threads[0].priority = 128;
    threads[0].pid = 1;
    thread_cpu_affinity[0] = 0;
    spinlock_release(&thread_alloc_lock);
    
    sched_enabled = 0;
    idle_entry = 0;
}

void scheduler_set_idle(void (*entry)(void)) {
    idle_entry = entry;
}

static int scheduler_create_from_template(void (*entry)(void), registers_t* tpl, uint32_t cpu_id) {
    if (!entry || !tpl) return -1;
    
    spinlock_acquire(&thread_alloc_lock);
    if (thread_count >= MAX_THREADS) {
        spinlock_release(&thread_alloc_lock);
        return -2;
    }
    int tid = thread_count++;
    spinlock_release(&thread_alloc_lock);

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

    threads[tid].rsp = (uint64_t*)frame;
    threads[tid].rsp_top = (uintptr_t)stack + THREAD_STACK_SIZE;
    threads[tid].pid = 0;
    threads[tid].state = THREAD_READY;
    threads[tid].priority = 128;
    thread_cpu_affinity[tid] = cpu_id;
    return tid;
}

int scheduler_create(void (*entry)(void)) {
    if (!entry) return -1;
    
    spinlock_acquire(&thread_alloc_lock);
    if (thread_count >= MAX_THREADS) {
        spinlock_release(&thread_alloc_lock);
        return -2;
    }
    int tid = thread_count++;
    spinlock_release(&thread_alloc_lock);

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

    uint64_t active_ss = 0x10;
    __asm__ __volatile__("mov %%ss, %0" : "=r" (active_ss));
    frame->ss = active_ss;
    frame->rsp = (uint64_t)(top + sizeof(registers_t));

    threads[tid].rsp = (uint64_t*)frame;
    threads[tid].rsp_top = (uintptr_t)stack + THREAD_STACK_SIZE;
    threads[tid].pid = 1;
    threads[tid].state = THREAD_READY;
    threads[tid].priority = 128;
    
    // Assign to a CPU round-robin
    static int next_cpu = 0;
    uint32_t cpus = smp_cpu_count();
    if (cpus == 0) cpus = 1;
    thread_cpu_affinity[tid] = next_cpu;
    next_cpu = (next_cpu + 1) % cpus;
    
    return tid;
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
    if (thread_id < 0 || thread_id >= MAX_THREADS) return -1;
    threads[thread_id].priority = priority;
    return 0;
}

void scheduler_thread_sleep(int thread_id, uint64_t ms) {
    if (thread_id < 0 || thread_id >= MAX_THREADS) return;
    uint32_t cpu = thread_cpu_affinity[thread_id];
    
    spinlock_acquire(&runqueues[cpu].lock);
    threads[thread_id].state = THREAD_SLEEPING;
    uint64_t ticks_to_wait = (ms * 100) / 1000u; // Assuming 100Hz
    if (ticks_to_wait == 0 && ms > 0) ticks_to_wait = 1;
    threads[thread_id].sleep_until_ticks = runqueues[cpu].ticks + ticks_to_wait;
    spinlock_release(&runqueues[cpu].lock);
    
    if (thread_id == scheduler_current_thread_id()) {
        scheduler_yield();
    }
}

void scheduler_thread_wake(int thread_id) {
    if (thread_id < 0 || thread_id >= MAX_THREADS) return;
    uint32_t cpu = thread_cpu_affinity[thread_id];
    
    spinlock_acquire(&runqueues[cpu].lock);
    if (threads[thread_id].state == THREAD_SLEEPING) {
        threads[thread_id].state = THREAD_READY;
    }
    spinlock_release(&runqueues[cpu].lock);
}

thread_state_t scheduler_get_thread_state(int thread_id) {
    if (thread_id < 0 || thread_id >= MAX_THREADS) return (thread_state_t)-1;
    return threads[thread_id].state;
}

uint32_t scheduler_get_cpu_id(void) {
    percpu_t* pc = percpu_get_current();
    return pc ? pc->cpu_id : 0;
}

int scheduler_current_thread_id(void) {
    uint32_t cpu_id = scheduler_get_cpu_id();
    if (cpu_id >= MAX_CPUS) return 0;
    return runqueues[cpu_id].current_idx;
}

int scheduler_block_current(void) {
    uint32_t cpu_id = scheduler_get_cpu_id();
    if (cpu_id >= MAX_CPUS) return -1;
    
    spinlock_acquire(&runqueues[cpu_id].lock);
    int tid = runqueues[cpu_id].current_idx;
    threads[tid].state = THREAD_BLOCKED;
    spinlock_release(&runqueues[cpu_id].lock);
    
    scheduler_yield();
    return 0;
}

int scheduler_unblock_thread(int thread_id) {
    if (thread_id < 0 || thread_id >= MAX_THREADS) return -1;
    uint32_t cpu = thread_cpu_affinity[thread_id];
    
    spinlock_acquire(&runqueues[cpu].lock);
    if (threads[thread_id].state == THREAD_BLOCKED) {
        threads[thread_id].state = THREAD_READY;
    }
    spinlock_release(&runqueues[cpu].lock);
    return 0;
}

void scheduler_yield(void) {
    __asm__ __volatile__("int $0x22"); // APIC Timer vector
}

void scheduler_ap_start(void) {
    uint32_t cpu_id = scheduler_get_cpu_id();
    if (cpu_id >= MAX_CPUS) return;

    spinlock_acquire(&runqueues[cpu_id].lock);

    int selected = -1;
    int tc = thread_count;
    for (int i = 0; i < tc; ++i) {
        if (thread_cpu_affinity[i] == cpu_id && threads[i].state == THREAD_READY && i != runqueues[cpu_id].idle_idx) {
            selected = i;
            break;
        }
    }

    if (selected == -1) {
        selected = (runqueues[cpu_id].idle_idx >= 0) ? runqueues[cpu_id].idle_idx : 0;
    }

    if (selected != runqueues[cpu_id].idle_idx && selected >= 0) {
        threads[selected].state = THREAD_RUNNING;
    }
    runqueues[cpu_id].current_idx = selected;

    registers_t* regs = 0;
    if (selected >= 0) {
        regs = (registers_t*)threads[selected].rsp;
        
        extern void tss_set_rsp0(uint64_t rsp0);
        tss_set_rsp0(threads[selected].rsp_top);
        
        percpu_t* pc = percpu_get_current();
        if (pc) pc->kernel_stack = threads[selected].rsp_top;
        
        uint32_t pid = threads[selected].pid;
        extern uint64_t process_get_page_directory(uint32_t pid);
        uint64_t cr3 = process_get_page_directory(pid);
        if (cr3) {
            __asm__ __volatile__("mov %0, %%cr3" :: "r"(cr3));
        }
    }
    
    spinlock_release(&runqueues[cpu_id].lock);

    if (regs) {
        __asm__ __volatile__("sti");
        context_restore(regs);
    } else {
        for(;;) __asm__ __volatile__("hlt");
    }
}

registers_t* scheduler_tick(registers_t* regs) {
    if (!sched_enabled) return regs;

    uint32_t cpu_id = scheduler_get_cpu_id();
    if (cpu_id >= MAX_CPUS) return regs;

    cpu_runqueue_t* rq = &runqueues[cpu_id];
    spinlock_acquire(&rq->lock);

    rq->ticks++;
    
    int cur_idx = rq->current_idx;
    
    if (cpu_id == 0 && cur_idx == 0 && threads[0].rsp == 0) {
        threads[0].rsp = (uint64_t*)regs;
    }

    if (rq->idle_idx < 0 && idle_entry) {
        int idx = scheduler_create_from_template(idle_entry, regs, cpu_id);
        if (idx >= 0) {
            rq->idle_idx = idx;
            threads[idx].state = THREAD_IDLE;
        }
    }

    int tc = thread_count;
    for (int i = 0; i < tc; ++i) {
        if (thread_cpu_affinity[i] == cpu_id && threads[i].state == THREAD_SLEEPING && 
            rq->ticks >= threads[i].sleep_until_ticks) {
            threads[i].state = THREAD_READY;
        }
    }

    threads[cur_idx].rsp = (uint64_t*)regs;
    if (threads[cur_idx].state == THREAD_RUNNING) {
        threads[cur_idx].state = THREAD_READY;
    }

    int next_idx = -1;
    int search_start = (cur_idx + 1) % tc;
    
    for (int i = 0; i < tc; ++i) {
        int idx = (search_start + i) % tc;
        if (idx == rq->idle_idx) continue;
        
        if (thread_cpu_affinity[idx] == cpu_id && threads[idx].state == THREAD_READY) {
            next_idx = idx;
            break;
        }
    }
    
    if (next_idx == -1) {
        uint32_t cores = smp_cpu_count();
        if (cores > 1) {
            for (uint32_t other = 0; other < cores; ++other) {
                if (other == cpu_id) continue;
                
                if (spinlock_try_acquire(&runqueues[other].lock)) {
                    for (int i = 0; i < tc; ++i) {
                        if (thread_cpu_affinity[i] == other && threads[i].state == THREAD_READY && i != runqueues[other].idle_idx) {
                            thread_cpu_affinity[i] = cpu_id;
                            next_idx = i;
                            break;
                        }
                    }
                    spinlock_release(&runqueues[other].lock);
                    if (next_idx != -1) break;
                }
            }
        }
    }

    if (next_idx == -1) {
        next_idx = (rq->idle_idx >= 0) ? rq->idle_idx : 0;
    }

    if (next_idx != rq->idle_idx && next_idx >= 0) {
        threads[next_idx].state = THREAD_RUNNING;
    }
    rq->current_idx = next_idx;

    registers_t* next_regs = regs;
    if (next_idx >= 0 && threads[next_idx].rsp) {
        next_regs = (registers_t*)threads[next_idx].rsp;
        
        extern void tss_set_rsp0(uint64_t rsp0);
        tss_set_rsp0(threads[next_idx].rsp_top);
        
        percpu_t* pc = percpu_get_current();
        if (pc) pc->kernel_stack = threads[next_idx].rsp_top;
        
        uint32_t pid = threads[next_idx].pid;
        extern uint64_t process_get_page_directory(uint32_t pid);
        uint64_t cr3 = process_get_page_directory(pid);
        if (cr3) {
            __asm__ __volatile__("mov %0, %%cr3" :: "r"(cr3));
        }
    }

    spinlock_release(&rq->lock);

    return next_regs;
}

int scheduler_create_user(void (*entry)(void), uint64_t user_rsp, uint32_t pid) {
    spinlock_acquire(&thread_alloc_lock);
    if (thread_count >= MAX_THREADS) {
        spinlock_release(&thread_alloc_lock);
        return -2;
    }
    int tid = thread_count++;
    spinlock_release(&thread_alloc_lock);

    uint8_t* stack = (uint8_t*)kmalloc(THREAD_STACK_SIZE);
    if (!stack) return -3;

    uintptr_t top = (uintptr_t)stack + THREAD_STACK_SIZE;
    top &= ~0xFUL;
    top -= sizeof(registers_t);
    registers_t* frame = (registers_t*)top;
    memzero(frame, sizeof(registers_t));
    
    frame->rip = (uint64_t)(uintptr_t)entry;
    frame->cs = 0x23; // User Code Selector | RPL 3
    frame->rflags = 0x202; // Interrupts enabled
    frame->rsp = user_rsp; // User stack!
    frame->ss = 0x1B; // User Data Selector | RPL 3
    frame->int_no = 32;
    frame->err_code = 0;

    threads[tid].rsp = (uint64_t*)frame;
    threads[tid].rsp_top = (uintptr_t)stack + THREAD_STACK_SIZE;
    threads[tid].pid = pid;
    threads[tid].state = THREAD_READY;
    threads[tid].priority = 128;

    // Assign to a CPU round-robin
    static int next_cpu = 0;
    uint32_t cpus = smp_cpu_count();
    if (cpus == 0) cpus = 1;
    thread_cpu_affinity[tid] = next_cpu;
    next_cpu = (next_cpu + 1) % cpus;

    return tid;
}

uint32_t scheduler_get_thread_pid(int thread_id) {
    if (thread_id < 0 || thread_id >= MAX_THREADS) return 0;
    return threads[thread_id].pid;
}

void scheduler_set_thread_pid(int thread_id, uint32_t pid) {
    if (thread_id < 0 || thread_id >= MAX_THREADS) return;
    threads[thread_id].pid = pid;
}

void scheduler_set_thread_state_idle(int thread_id) {
    if (thread_id < 0 || thread_id >= MAX_THREADS) return;
    threads[thread_id].state = THREAD_IDLE;
}

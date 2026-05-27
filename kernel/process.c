#include "process.h"
#include "memory.h"
#include "serial.h"
#include "spinlock.h"
#include "sched.h"
#include "timer.h"
static process_t processes[MAX_PROCESSES];
spinlock_t process_lock = SPINLOCK_INIT;
uint32_t next_pid = 1;

static inline uint64_t read_cr3(void) {
    uint64_t cr3;
    __asm__ __volatile__("mov %%cr3, %0" : "=r"(cr3));
    return cr3;
}

uint64_t process_get_page_directory(uint32_t pid) {
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (processes[i].pid == pid && processes[i].state != PROC_UNUSED) {
            return processes[i].page_directory;
        }
    }
    return 0;
}

process_t* process_get_table(void) {
    return processes;
}

uint64_t process_create_pml4(void) {
    paddr_t pml4_phys = pmm_alloc_frame();
    if (!pml4_phys) return 0;
    
    uint64_t* pml4 = (uint64_t*)(uintptr_t)pml4_phys;
    for (int i = 0; i < 512; i++) pml4[i] = 0;
    
    // Copy kernel's PML4 identity entries
    uint64_t kernel_pml4_phys = read_cr3();
    uint64_t* kernel_pml4 = (uint64_t*)(uintptr_t)kernel_pml4_phys;
    for (int i = 0; i < 512; i++) {
        pml4[i] = kernel_pml4[i];
    }
    
    return pml4_phys;
}

static uint64_t build_process_paging(void) {
    uint64_t pml4_phys = process_create_pml4();
    if (!pml4_phys) return 0;
    
    uint64_t* pml4 = (uint64_t*)(uintptr_t)pml4_phys;
    
    // 4. Create custom PDPT in pml4[1] for user mode (covering 512 GiB - 1024 GiB)
    paddr_t pdpt_phys = pmm_alloc_frame();
    if (!pdpt_phys) return pml4_phys;
    uint64_t* pdpt = (uint64_t*)(uintptr_t)pdpt_phys;
    for (int i = 0; i < 512; i++) pdpt[i] = 0;
    
    pml4[1] = pdpt_phys | 0x07u; // Present | R/W | User
    
    // 5. Create custom PD in pdpt[0]
    paddr_t pd_phys = pmm_alloc_frame();
    if (!pd_phys) return pml4_phys;
    uint64_t* pd = (uint64_t*)(uintptr_t)pd_phys;
    for (int i = 0; i < 512; i++) pd[i] = 0;
    
    pdpt[0] = pd_phys | 0x07u; // Present | R/W | User
    
    // 6. Map first 8MB as user-accessible (identity mirror under PML4[1] -> 0x8000000000)
    pd[0] = 0x00000000 | 0x87u; // 0MB - 2MB: Present | R/W | User | 2MB Page Size
    pd[1] = 0x00200000 | 0x87u; // 2MB - 4MB: Present | R/W | User | 2MB Page Size
    pd[2] = 0x00400000 | 0x87u; // 4MB - 6MB: Present | R/W | User | 2MB Page Size
    pd[3] = 0x00600000 | 0x87u; // 6MB - 8MB: Present | R/W | User | 2MB Page Size
    
    // 7. Create stack page table (PT) in PD entry 511
    paddr_t pt_phys = pmm_alloc_frame();
    if (!pt_phys) return pml4_phys;
    uint64_t* pt = (uint64_t*)(uintptr_t)pt_phys;
    for (int i = 0; i < 512; i++) pt[i] = 0;
    
    pd[511] = pt_phys | 0x07u; // Present | R/W | User
    
    // 8. Allocate stack physical page and map in last entry of PT (entry 511)
    paddr_t stack_phys = pmm_alloc_frame();
    if (!stack_phys) return pml4_phys;
    pt[511] = stack_phys | 0x07u; // Present | R/W | User
    
    return pml4_phys;
}

void process_init(void) {
    spinlock_acquire(&process_lock);
    for (int i = 0; i < MAX_PROCESSES; i++) {
        processes[i].pid = 0;
        processes[i].ppid = 0;
        processes[i].state = PROC_UNUSED;
        processes[i].exit_code = 0;
        processes[i].page_directory = 0;
        processes[i].user_stack_base = 0;
        processes[i].user_entry = 0;
        processes[i].is_user = 0;
        processes[i].main_thread_id = -1;
        for (int fd = 0; fd < 16; fd++) processes[i].fds[fd] = -1;
    }
    
    // Create PCB for BSP main thread (PID 1)
    processes[0].pid = 1;
    processes[0].ppid = 0;
    processes[0].state = PROC_RUNNING;
    processes[0].page_directory = read_cr3();
    processes[0].is_user = 0;
    processes[0].main_thread_id = 0;
    
    next_pid = 2;
    spinlock_release(&process_lock);
    serial_writeln("[process] Subsystem initialized (PID 1 active)");
}

process_t* process_get_current(void) {
    int tid = scheduler_current_thread_id();
    // In our system, threads map to processes by PID. We look up by the running thread's PID.
    extern uint32_t scheduler_get_thread_pid(int thread_id);
    uint32_t pid = scheduler_get_thread_pid(tid);
    
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (processes[i].pid == pid && processes[i].state != PROC_UNUSED) {
            return &processes[i];
        }
    }
    return &processes[0]; // Fallback to PID 1
}

extern int scheduler_create_user(void (*entry)(void), uint64_t user_rsp, uint32_t pid);

int process_spawn_elf(uint64_t pml4, uint64_t entry) {
    spinlock_acquire(&process_lock);
    int slot = -1;
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (processes[i].state == PROC_UNUSED) {
            slot = i;
            break;
        }
    }
    if (slot == -1) {
        spinlock_release(&process_lock);
        return -1;
    }
    
    uint32_t pid = next_pid++;
    processes[slot].pid = pid;
    processes[slot].ppid = 1;
    processes[slot].state = PROC_READY;
    processes[slot].is_user = 1;
    for (int fd = 0; fd < 16; fd++) processes[slot].fds[fd] = -1;
    
    processes[slot].page_directory = pml4;
    
    // Create stack for user process
    uint64_t v_stack_base = 0x803FEFD000;
    paddr_t stack_phys = pmm_alloc_frame();
    vmm_map(pml4, v_stack_base - PAGE_SIZE, stack_phys, 0x07);
    
    processes[slot].user_stack_base = v_stack_base;
    processes[slot].user_entry = entry;
    
    int tid = scheduler_create_user((void(*)(void))entry, v_stack_base, pid);
    processes[slot].main_thread_id = tid;
    
    spinlock_release(&process_lock);
    return (int)pid;
}

int process_spawn(void (*entry)(void), int is_user) {
    spinlock_acquire(&process_lock);
    int slot = -1;
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (processes[i].state == PROC_UNUSED) {
            slot = i;
            break;
        }
    }
    if (slot == -1) {
        spinlock_release(&process_lock);
        return -1;
    }
    
    uint32_t pid = next_pid++;
    processes[slot].pid = pid;
    processes[slot].ppid = 1; // BSP is parent
    processes[slot].state = PROC_READY;
    processes[slot].is_user = is_user;
    for (int fd = 0; fd < 16; fd++) processes[slot].fds[fd] = -1;
    
    if (is_user) {
        // Build private paging structures
        processes[slot].page_directory = build_process_paging();
        // Virtual stack top is 512 GiB + 1022 MiB + 2048 KiB = 0x803FEFD000
        processes[slot].user_stack_base = 0x803FEFD000;
        // User entry point is aliased under the 512 GiB mark (0x8000000000)
        processes[slot].user_entry = (uint64_t)entry | 0x8000000000ULL;
        
        // Spawn user thread in scheduler
        int tid = scheduler_create_user((void(*)(void))processes[slot].user_entry, processes[slot].user_stack_base, pid);
        processes[slot].main_thread_id = tid;
    } else {
        // Kernel process uses normal kernel page directory
        processes[slot].page_directory = read_cr3();
        processes[slot].user_stack_base = 0;
        processes[slot].user_entry = (uint64_t)entry;
        
        int tid = scheduler_create(entry);
        extern void scheduler_set_thread_pid(int thread_id, uint32_t pid);
        scheduler_set_thread_pid(tid, pid);
        processes[slot].main_thread_id = tid;
    }
    
    spinlock_release(&process_lock);
    
    char pbuf[32];
    {
        uint32_t v = pid; int n = 0; char tmp[32];
        while(v) { tmp[n++] = '0' + (v % 10); v /= 10; }
        int l = 0; while(n--) pbuf[l++] = tmp[n]; pbuf[l] = 0;
    }
    serial_write("[process] Spawned PID "); serial_writeln(pbuf);
    
    return (int)pid;
}

void process_exit(uint64_t code) {
    process_t* proc = process_get_current();
    if (proc->pid <= 1) {
        // Cannot exit BSP
        serial_write("[process] Error: BSP (PID "); serial_u64(proc->pid); serial_writeln(") cannot exit!");
        for(;;);
    }
    
    spinlock_acquire(&process_lock);
    proc->state = PROC_ZOMBIE;
    proc->exit_code = code;
    spinlock_release(&process_lock);
    
    char pbuf[32], cbuf[32];
    {
        uint32_t v = proc->pid; int n = 0; char tmp[32];
        while(v) { tmp[n++] = '0' + (v % 10); v /= 10; }
        int l = 0; while(n--) pbuf[l++] = tmp[n]; pbuf[l] = 0;
    }
    {
        uint64_t v = code; int n = 0; char tmp[32];
        if (v == 0) { cbuf[0] = '0'; cbuf[1] = 0; }
        else {
            while(v) { tmp[n++] = '0' + (v % 10); v /= 10; }
            int l = 0; while(n--) cbuf[l++] = tmp[n]; cbuf[l] = 0;
        }
    }
    serial_write("[process] PID "); serial_write(pbuf);
    serial_write(" exited with code "); serial_writeln(cbuf);
    
    // Set thread state to idle so scheduler skips it
    extern void scheduler_set_thread_state_idle(int thread_id);
    scheduler_set_thread_state_idle(proc->main_thread_id);
    
    scheduler_yield();
}

int process_wait(uint32_t pid, uint64_t* exit_code) {
    for (;;) {
        spinlock_acquire(&process_lock);
        int found = 0;
        for (int i = 0; i < MAX_PROCESSES; i++) {
            if (processes[i].pid == pid && processes[i].state != PROC_UNUSED) {
                found = 1;
                if (processes[i].state == PROC_ZOMBIE) {
                    if (exit_code) *exit_code = processes[i].exit_code;
                    processes[i].state = PROC_UNUSED; // Free the PCB
                    processes[i].pid = 0;
                    spinlock_release(&process_lock);
                    return 0;
                }
            }
        }
        if (!found) {
            spinlock_release(&process_lock);
            return -1; // Process doesn't exist
        }
        spinlock_release(&process_lock);
        
        // Wait and yield
        timer_sleep(10);
    }
}

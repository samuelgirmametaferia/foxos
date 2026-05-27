#include "syscall.h"
#include "memory.h"
#include "serial.h"
#include "console.h"
#include "vfs.h"
#include "process.h"
#include "sched.h"
#include "timer.h"

#define MSR_EFER   0xC0000080
#define MSR_STAR   0xC0000081
#define MSR_LSTAR  0xC0000082
#define MSR_FMASK  0xC0000084

#define SYS_EXIT    1
#define SYS_WRITE   2
#define SYS_READ    3
#define SYS_SPAWN   4
#define SYS_YIELD   5
#define SYS_SLEEP   6
#define SYS_GETPID  7
#define SYS_OPEN    8
#define SYS_CLOSE   9
#define SYS_IOCTL   10
#define SYS_MMAP    11
#define SYS_EXEC    12
#define SYS_GETINFO 13

typedef struct {
    uint32_t cpu_count;
    uint32_t total_mem_mb;
    uint32_t free_mem_mb;
    uint32_t process_count;
    uint64_t uptime_ms;
    uint32_t fb_width;
    uint32_t fb_height;
} sys_info_t;

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
    wrmsr(MSR_EFER, efer | 1);

    uint64_t star = ((uint64_t)0x10 << 48) | ((uint64_t)0x08 << 32);
    wrmsr(MSR_STAR, star);
    wrmsr(MSR_LSTAR, (uint64_t)syscall_entry);
    wrmsr(MSR_FMASK, 0x600);
}

void syscall_init_ap(void) {
    uint64_t efer = rdmsr(MSR_EFER);
    wrmsr(MSR_EFER, efer | 1);
    uint64_t star = ((uint64_t)0x10 << 48) | ((uint64_t)0x08 << 32);
    wrmsr(MSR_STAR, star);
    wrmsr(MSR_LSTAR, (uint64_t)syscall_entry);
    wrmsr(MSR_FMASK, 0x600);
}

uint64_t syscall_handler(uint64_t rdi, uint64_t rsi, uint64_t rdx, uint64_t r10, uint64_t r8, uint64_t r9, uint64_t sys_num) {
    (void)r10; (void)r8; (void)r9;
    
    /* SYSCALL masks interrupts, re-enable them so functions like timer_sleep work */
    __asm__ __volatile__("sti");

    uint64_t ret = (uint64_t)-1;
    
    switch (sys_num) {
        case SYS_EXIT:
            process_exit(rdi);
            ret = 0;
            break;
            
        case SYS_WRITE: {
            int fd = (int)rdi;
            const char* buf = (const char*)rsi;
            uint64_t count = rdx;
            
            if (fd == 1 || fd == 2) {
                // stdout / stderr: print to console and serial
                for (uint64_t i = 0; i < count; i++) {
                    console_putc(buf[i]);
                    serial_putc(buf[i]);
                }
                ret = count;
            } else {
                ret = (uint64_t)sys_write(fd, buf, count);
            }
            break;
        }
            
        case SYS_READ: {
            int fd = (int)rdi;
            char* buf = (char*)rsi;
            uint64_t count = rdx;
            
            if (fd == 0) {
                // stdin: read from keyboard
                extern int keyboard_getchar(void);
                uint64_t read_bytes = 0;
                while (read_bytes < count) {
                    int ch = keyboard_getchar();
                    if (ch != -1) {
                        buf[read_bytes++] = (char)ch;
                    } else {
                        // Yield or sleep so we don't hog the CPU in user mode!
                        timer_sleep(10);
                    }
                }
                ret = read_bytes;
            } else {
                ret = (uint64_t)sys_read(fd, buf, count);
            }
            break;
        }
            
        case SYS_SPAWN:
            ret = (uint64_t)process_spawn((void(*)(void))rdi, 1);
            break;
            
        case SYS_YIELD:
            scheduler_yield();
            ret = 0;
            break;
            
        case SYS_SLEEP:
            scheduler_thread_sleep(scheduler_current_thread_id(), rdi);
            ret = 0;
            break;
            
        case SYS_GETPID:
            ret = (uint64_t)process_get_current()->pid;
            break;
            
        case SYS_OPEN:
            ret = (uint64_t)sys_open((const char*)rdi, (int)rsi, (int)rdx);
            break;
            
        case SYS_CLOSE:
            ret = (uint64_t)sys_close((int)rdi);
            break;
            
        case SYS_IOCTL:
            ret = (uint64_t)sys_ioctl((int)rdi, (int)rsi, (void*)rdx);
            break;
            
        case SYS_MMAP:
            ret = (uint64_t)sys_mmap((int)rdi, rsi, rdx);
            break;
            
        case SYS_EXEC: {
            extern int sys_exec(const char* path, char* const argv[], char* const envp[]);
            ret = (uint64_t)sys_exec((const char*)rdi, (char* const*)rsi, (char* const*)rdx);
            break;
        }
            
        case SYS_GETINFO: {
            sys_info_t* info = (sys_info_t*)rdi;
            if (!info) { ret = (uint64_t)-1; break; }
            
            extern uint32_t smp_cpu_count(void);
            info->cpu_count = smp_cpu_count();
            info->total_mem_mb = (uint32_t)(pmm_total_pages() * 4096 / 1024 / 1024);
            info->free_mem_mb = (uint32_t)(pmm_free_pages() * 4096 / 1024 / 1024);
            
            extern uint32_t next_pid; 
            info->process_count = next_pid - 1; 
            
            info->uptime_ms = timer_get_ticks(); 
            
            extern uint32_t gpu_get_width(void);
            extern uint32_t gpu_get_height(void);
            info->fb_width = gpu_get_width();
            info->fb_height = gpu_get_height();
            
            ret = 0;
            break;
        }
            
        default:
            serial_writeln("[syscall] unknown syscall");
            ret = (uint64_t)-1;
            break;
    }

    __asm__ __volatile__("cli");
    return ret;
}

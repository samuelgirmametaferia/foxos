#pragma once
#include <stdint.h>

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
    uint32_t fb_stride;
} sys_info_t;

static inline uint64_t syscall(uint64_t sys_num, uint64_t a1, uint64_t a2, uint64_t a3) {
    uint64_t ret;
    __asm__ __volatile__(
        "syscall"
        : "=a"(ret)
        : "a"(sys_num), "D"(a1), "S"(a2), "d"(a3)
        : "rcx", "r11", "memory"
    );
    return ret;
}

static inline int sys_open(const char* path, int flags, int mode) {
    return (int)syscall(SYS_OPEN, (uint64_t)path, flags, mode);
}

static inline int sys_read(int fd, void* buf, uint64_t count) {
    return (int)syscall(SYS_READ, fd, (uint64_t)buf, count);
}

static inline int sys_write(int fd, const void* buf, uint64_t count) {
    return (int)syscall(SYS_WRITE, fd, (uint64_t)buf, count);
}

static inline void* sys_mmap(int fd, uint64_t length, uint64_t offset) {
    return (void*)syscall(SYS_MMAP, fd, length, offset);
}

static inline int sys_close(int fd) {
    return (int)syscall(SYS_CLOSE, fd, 0, 0);
}

static inline int sys_ioctl(int fd, int cmd, void* arg) {
    return (int)syscall(SYS_IOCTL, fd, cmd, (uint64_t)arg);
}

static inline void sys_exit(int code) {
    syscall(SYS_EXIT, code, 0, 0);
}

static inline void sys_yield(void) {
    syscall(SYS_YIELD, 0, 0, 0);
}

static inline int sys_getinfo(sys_info_t* info) {
    return (int)syscall(SYS_GETINFO, (uint64_t)info, 0, 0);
}

static inline void sys_sleep(uint64_t ms) {
    syscall(SYS_SLEEP, ms, 0, 0);
}

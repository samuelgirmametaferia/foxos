#pragma once
#include <stdint.h>
#include "sched.h"
#include "idt.h"

#define MAX_PROCESSES 64
#define USER_STACK_SIZE (64u * 1024u)

typedef enum {
    PROC_UNUSED = 0,
    PROC_EMBRYO,
    PROC_RUNNING,
    PROC_READY,
    PROC_BLOCKED,
    PROC_ZOMBIE
} proc_state_t;

typedef struct {
    uint32_t pid;
    uint32_t ppid;
    proc_state_t state;
    uint64_t exit_code;
    uint64_t page_directory;  // PML4 CR3
    uint64_t user_stack_base; // Alloc base
    uint64_t user_entry;      // RIP entry
    int is_user;
    int fds[16];
    int main_thread_id;
} process_t;

void process_init(void);
uint64_t process_create_pml4(void);
int process_spawn(void (*entry)(void), int is_user);
int process_spawn_elf(uint64_t pml4, uint64_t entry);
void process_exit(uint64_t code);
int process_wait(uint32_t pid, uint64_t* exit_code);
process_t* process_get_current(void);
uint64_t process_get_page_directory(uint32_t pid);
process_t* process_get_table(void);

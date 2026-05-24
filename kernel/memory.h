#pragma once
#include <stdint.h>
#include "boot.h"

#define PAGE_SIZE 4096u

void mem_init(const boot_info_t* boot);

typedef uint64_t paddr_t;
typedef uint64_t vaddr_t;

paddr_t pmm_alloc_frame(void);
paddr_t pmm_alloc_contiguous_pages(uint64_t pages);
void pmm_free_frame(paddr_t addr);
void pmm_free_contiguous_pages(paddr_t addr, uint64_t pages);
uint64_t pmm_total_pages(void);
uint64_t pmm_free_pages(void);

void* kmalloc(uint32_t size);
void* kcalloc(uint32_t count, uint32_t size);
void* krealloc(void* ptr, uint32_t size);
void kfree(void* ptr);

typedef uint32_t uchandle_t;

uchandle_t uc_alloc(uint64_t bytes);
int uc_free(uchandle_t h);
int uc_write(uchandle_t h, const void* src, uint64_t len);
int uc_read(uchandle_t h, uint64_t offset, void* dst, uint64_t len);

uint64_t uc_size(uchandle_t h);
uint64_t uc_used(uchandle_t h);

/* Expose helpers for paging setup */
void setup_identity_paging(void);
uint64_t pmm_phys_end_bytes(void);

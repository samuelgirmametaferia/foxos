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

/* Page pinning API for DMA */
void pmm_pin_page(paddr_t paddr);
void pmm_unpin_page(paddr_t paddr);
int pmm_is_page_pinned(paddr_t paddr);


void* kmalloc(uint32_t size);
void* kcalloc(uint32_t count, uint32_t size);
void* krealloc(void* ptr, uint32_t size);
void kfree(void* ptr);

void memzero(void* ptr, uint32_t len);
void memcopy(void* dest, const void* src, uint32_t len);

typedef uint32_t uchandle_t;

uchandle_t uc_alloc(uint64_t bytes);
int uc_free(uchandle_t h);
int uc_write(uchandle_t h, const void* src, uint64_t len);
int uc_read(uchandle_t h, uint64_t offset, void* dst, uint64_t len);
int uc_defragment(uchandle_t h);
int uc_defrag_all(void);

uint64_t uc_size(uchandle_t h);
uint64_t uc_used(uchandle_t h);

/* Expose helpers for paging setup */
void setup_identity_paging(void);
void vmm_map(uint64_t pml4_phys, uint64_t vaddr, uint64_t paddr, uint64_t flags);
uint64_t pmm_phys_end_bytes(void);

/* Debug helpers */
void pmm_dump_stats(void);

/* Heap maintenance */
void heap_shrink_all(void);

/* Moveable allocation API (thin wrapper over UC) for relocatable kernel allocations */
typedef uint32_t movehandle_t;
movehandle_t move_alloc(uint64_t bytes);
int move_free(movehandle_t h);
int move_write(movehandle_t h, const void* src, uint64_t len);
int move_read(movehandle_t h, uint64_t offset, void* dst, uint64_t len);
int move_defrag_all(void);

/* Moveable-backed kernel allocation helpers (new) */
movehandle_t move_kmalloc(uint32_t size);
int move_kfree(movehandle_t h);

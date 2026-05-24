#include <stdint.h>
#include <stddef.h>
#include "memory.h"
#include "boot.h"
#include "serial.h"

#define ALIGN_UP(value, align) (((value) + ((align) - 1u)) & ~((align) - 1u))
#define MIN_HEAP_SPLIT 8u

extern uint8_t __bss_end;

static void serial_u64(uint64_t v) {
    char buf[32]; int n=0; if (v==0) { buf[n++]='0'; buf[n]=0; serial_write(buf); return; }
    char tmp[32]; int t=0; while(v){ tmp[t++]=(char)('0'+(v%10)); v/=10; }
    while(t--) buf[n++]=tmp[t]; buf[n]=0; serial_write(buf);
}

static void serial_u32(uint32_t v) {
    serial_u64((uint64_t)v);
}


typedef struct heap_block {
    uint32_t size;
    uint32_t free;
    struct heap_block* next;
    struct heap_block* prev;
} heap_block_t;

typedef struct {
    uint32_t magic;
    uint32_t total_chunks;
    uint64_t used_bytes;
    uint32_t chunk_idx[1024];
} ucdesc_t;

#define MAX_UC 128

static uint32_t* pmm_bitmap = NULL;           /* dynamically allocated in physical memory area */
static uint64_t pmm_bitmap_words = 0;
static uint64_t pmm_total = 0;
static uint64_t pmm_free = 0;
static uintptr_t pmm_base = 0;                 /* physical address of first managed page */
static uint64_t pmm_phys_end = 0;              /* highest physical address considered (bytes) */
static uint64_t pmm_total_conv_pages = 0;        /* total conventional pages reported by firmware */

static heap_block_t* heap_head = NULL;
static ucdesc_t uc_table[MAX_UC];
static uint32_t rng_state = 0xC0FFEE01u;

static uintptr_t align_up_ptr(uintptr_t value, uintptr_t align) {
    return (value + (align - 1u)) & ~(align - 1u);
}

static uint32_t rnd32(void) {
    rng_state = rng_state * 1664525u + 1013904223u;
    return rng_state;
}

static inline uint64_t frame_index_for_addr(paddr_t addr) {
    return (uint64_t)((addr - (paddr_t)pmm_base) / PAGE_SIZE);
}

static inline paddr_t addr_for_frame_index(uint64_t idx) {
    return (paddr_t)(pmm_base + ((uintptr_t)idx * PAGE_SIZE));
}

static inline uint64_t bit_word(uint64_t idx) { return idx >> 5; }
static inline uint32_t bit_mask(uint64_t idx) { return 1u << (idx & 31u); }

static int frame_is_used(uint64_t idx) {
    uint64_t w = bit_word(idx);
    if (w >= pmm_bitmap_words) return 1;
    return (pmm_bitmap[w] & bit_mask(idx)) != 0;
}

static void frame_mark_used(uint64_t idx) {
    uint64_t w = bit_word(idx);
    uint32_t mask = bit_mask(idx);
    if (w >= pmm_bitmap_words) return;
    if (!(pmm_bitmap[w] & mask)) {
        pmm_bitmap[w] |= mask;
        if (pmm_free > 0) pmm_free--;
    }
}

static void frame_mark_free(uint64_t idx) {
    uint64_t w = bit_word(idx);
    uint32_t mask = bit_mask(idx);
    if (w >= pmm_bitmap_words) return;
    if (pmm_bitmap[w] & mask) {
        pmm_bitmap[w] &= (uint32_t)~mask;
        pmm_free++;
    }
}

static void pmm_reserve_range(paddr_t addr, uint64_t size) {
    if (addr < (paddr_t)pmm_base || size == 0) return;
    paddr_t end = addr + size;
    if (end < addr) return;
    paddr_t limit = (paddr_t)(pmm_base + (pmm_total * PAGE_SIZE));
    if (end > limit) end = limit;
    uint64_t start_idx = frame_index_for_addr((paddr_t)ALIGN_UP(addr, PAGE_SIZE));
    uint64_t end_idx = (uint64_t)((end - pmm_base) / PAGE_SIZE);
    for (uint64_t i = start_idx; i < end_idx && i < pmm_total; ++i) frame_mark_used(i);
}

static void pmm_release_range(paddr_t addr, uint64_t size) {
    if (addr < (paddr_t)pmm_base || size == 0) return;
    paddr_t end = addr + size;
    if (end < addr) return;
    paddr_t limit = (paddr_t)(pmm_base + (pmm_total * PAGE_SIZE));
    if (end > limit) end = limit;
    uint64_t start_idx = frame_index_for_addr((paddr_t)ALIGN_UP(addr, PAGE_SIZE));
    uint64_t end_idx = (uint64_t)((end - pmm_base) / PAGE_SIZE);
    for (uint64_t i = start_idx; i < end_idx && i < pmm_total; ++i) frame_mark_free(i);
}

static int contiguous_run_is_free(uint64_t start, uint64_t pages) {
    if (pages == 0 || start >= pmm_total || pages > pmm_total - start) return 0;
    for (uint64_t i = 0; i < pages; ++i) {
        if (frame_is_used(start + i)) return 0;
    }
    return 1;
}

/* heap management (uses pmm_alloc_contiguous_pages for growth) */
static void heap_insert_block(heap_block_t* block) {
    heap_block_t* cur = heap_head;
    heap_block_t* prev = NULL;

    while (cur && cur < block) {
        prev = cur;
        cur = cur->next;
    }

    block->prev = prev;
    block->next = cur;
    block->free = 1;

    if (prev) prev->next = block; else heap_head = block;
    if (cur) cur->prev = block;
}

static int heap_blocks_touch(heap_block_t* left, heap_block_t* right) {
    uint8_t* expected = (uint8_t*)left + sizeof(heap_block_t) + left->size;
    return expected == (uint8_t*)right;
}

static void heap_merge_with_next(heap_block_t* block) {
    heap_block_t* next = block->next;
    if (!next || !next->free || !heap_blocks_touch(block, next)) return;

    block->size += (uint32_t)(sizeof(heap_block_t) + next->size);
    block->next = next->next;
    if (next->next) next->next->prev = block;
}

static void heap_merge_with_prev(heap_block_t* block) {
    heap_block_t* prev = block->prev;
    if (!prev || !prev->free || !heap_blocks_touch(prev, block)) return;

    prev->size += (uint32_t)(sizeof(heap_block_t) + block->size);
    prev->next = block->next;
    if (block->next) block->next->prev = prev;
}

static int heap_grow(uint32_t min_bytes) {
    uint32_t needed = min_bytes + (uint32_t)sizeof(heap_block_t);
    uint32_t pages = (needed + PAGE_SIZE - 1u) / PAGE_SIZE;
    if (pages == 0) pages = 1;

    paddr_t addr = pmm_alloc_contiguous_pages(pages);
    if (!addr) return -1;

    heap_block_t* block = (heap_block_t*)addr;
    block->size = pages * PAGE_SIZE - (uint32_t)sizeof(heap_block_t);
    block->free = 1;
    block->next = NULL;
    block->prev = NULL;

    heap_insert_block(block);
    heap_merge_with_prev(block);
    heap_merge_with_next(block);
    return 0;
}

static heap_block_t* heap_find_block(uint32_t size) {
    heap_block_t* cur = heap_head;
    while (cur) {
        if (cur->free && cur->size >= size) return cur;
        cur = cur->next;
    }
    return NULL;
}

static void* heap_alloc(uint32_t size) {
    uint32_t aligned = ALIGN_UP(size, 8u);
    heap_block_t* block = heap_find_block(aligned);
    while (!block) {
        if (heap_grow(aligned) != 0) return NULL;
        block = heap_find_block(aligned);
    }

    uint32_t remaining = block->size - aligned;
    if (remaining >= sizeof(heap_block_t) + MIN_HEAP_SPLIT) {
        heap_block_t* split = (heap_block_t*)((uint8_t*)(block + 1) + aligned);
        split->size = remaining - (uint32_t)sizeof(heap_block_t);
        split->free = 1;
        split->next = block->next;
        split->prev = block;
        if (block->next) block->next->prev = split;
        block->next = split;
        block->size = aligned;
    }

    block->free = 0;
    return (void*)(block + 1);
}

static void heap_free_block(void* ptr) {
    if (!ptr) return;

    heap_block_t* block = ((heap_block_t*)ptr) - 1;
    block->free = 1;
    heap_merge_with_next(block);
    heap_merge_with_prev(block);
}

static void* heap_realloc_block(void* ptr, uint32_t size) {
    if (!ptr) return heap_alloc(size);
    if (size == 0) {
        heap_free_block(ptr);
        return NULL;
    }

    heap_block_t* block = ((heap_block_t*)ptr) - 1;
    uint32_t aligned = ALIGN_UP(size, 8u);

    if (block->size >= aligned) {
        uint32_t remaining = block->size - aligned;
        if (remaining >= sizeof(heap_block_t) + MIN_HEAP_SPLIT) {
            heap_block_t* split = (heap_block_t*)((uint8_t*)(block + 1) + aligned);
            split->size = remaining - (uint32_t)sizeof(heap_block_t);
            split->free = 1;
            split->next = block->next;
            split->prev = block;
            if (block->next) block->next->prev = split;
            block->next = split;
            block->size = aligned;
            heap_merge_with_next(split);
        }
        return ptr;
    }

    void* next = heap_alloc(aligned);
    if (!next) return NULL;

    uint32_t copy = block->size < aligned ? block->size : aligned;
    const uint8_t* src = (const uint8_t*)ptr;
    uint8_t* dst = (uint8_t*)next;
    for (uint32_t i = 0; i < copy; ++i) dst[i] = src[i];
    heap_free_block(ptr);
    return next;
}

static void memzero(void* ptr, uint32_t len) {
    uint8_t* p = (uint8_t*)ptr;
    for (uint32_t i = 0; i < len; ++i) p[i] = 0;
}

static void memcopy(void* dst, const void* src, uint32_t len) {
    uint8_t* d = (uint8_t*)dst;
    const uint8_t* s = (const uint8_t*)src;
    for (uint32_t i = 0; i < len; ++i) d[i] = s[i];
}

static ucdesc_t* get_uc(uchandle_t h) {
    if (h == 0) return NULL;
    ucdesc_t* d = &uc_table[h % MAX_UC];
    if (d->magic != h) return NULL;
    return d;
}

static paddr_t frame_addr_from_chunk(uint32_t idx) {
    return addr_for_frame_index((uint64_t)idx);
}

void mem_init(const boot_info_t* boot) {
    serial_writeln("[mem] mem_init start");
    if (boot && boot->magic == FOX_BOOT_INFO_MAGIC) {
        serial_write("[mem] memory_map_count: "); serial_u64(boot->memory_map_count); serial_writeln("");
        if (boot->memory_map_count > 0 && boot->memory_map) {
            serial_write("[mem] first region: type="); serial_u64(boot->memory_map[0].type);
            serial_write(" start="); serial_u64(boot->memory_map[0].physical_start);
            serial_write(" pages="); serial_u64(boot->memory_map[0].page_count);
            serial_writeln("");
        }
    }

    uintptr_t initial = align_up_ptr((uintptr_t)&__bss_end, PAGE_SIZE);

    /* determine highest physical address from memory map */
    uint64_t phys_end = initial;
    if (boot && boot->magic == FOX_BOOT_INFO_MAGIC && boot->memory_map && boot->memory_map_count) {
        uint64_t total_conv_pages = 0;
        serial_writeln("[mem] dumping memory map:");
        for (uint64_t i = 0; i < boot->memory_map_count; ++i) {
            const boot_memory_region_t* region = &boot->memory_map[i];
            uint64_t end = region->physical_start + (region->page_count * PAGE_SIZE);
            /* ignore reserved type 0 when computing phys_end to avoid exaggerated address ranges */
            if (region->type != BOOT_MEMORY_TYPE_RESERVED) {
                if (end > phys_end) phys_end = end;
            }
            serial_write("[mem] region "); serial_u64(i); serial_write(": type="); serial_u64(region->type);
            serial_write(" start="); serial_u64(region->physical_start);
            serial_write(" pages="); serial_u64(region->page_count);
            serial_writeln("");
            if (region->type == BOOT_MEMORY_TYPE_CONVENTIONAL) total_conv_pages += region->page_count;
        }
        serial_write("[mem] total conventional pages: "); serial_u64(total_conv_pages); serial_writeln("");
        /* record conventional pages for paging target */
        pmm_total_conv_pages = total_conv_pages;
        serial_write("[mem] highest considered phys_end: "); serial_u64(phys_end); serial_writeln("");
    } else {
        /* fallback: limit to initial + 1GB */
        phys_end = initial + (1ull * 1024ull * 1024ull * 1024ull);
    }

    /* record phys_end globally for paging setup */
    pmm_phys_end = phys_end;

    /* allocate bitmap area immediately after kernel BSS */
    uint64_t tentative_frames = (phys_end > initial) ? ((phys_end - initial) / PAGE_SIZE) : 0;
    uint64_t tentative_words = (tentative_frames + 31u) / 32u;
    uint64_t bitmap_bytes = ALIGN_UP(tentative_words * sizeof(uint32_t), PAGE_SIZE);
    uintptr_t bitmap_addr = initial;
    /* If the firmware provided a handoff block, place the bitmap after it to avoid overlap */
    if (boot && boot->magic == FOX_BOOT_INFO_MAGIC && boot->handoff_base && boot->handoff_size) {
        uintptr_t handoff_end = (uintptr_t)(boot->handoff_base + boot->handoff_size);
        if (handoff_end > bitmap_addr) bitmap_addr = align_up_ptr(handoff_end, PAGE_SIZE);
    }
    uintptr_t new_base = align_up_ptr(bitmap_addr + bitmap_bytes, PAGE_SIZE);

    /* now compute actual managed frames starting after bitmap */
    pmm_base = new_base;
    pmm_total = (phys_end > pmm_base) ? ((phys_end - pmm_base) / PAGE_SIZE) : 0;
    pmm_bitmap_words = (pmm_total + 31u) / 32u;
    if (pmm_total == 0) {
        pmm_bitmap = NULL;
        pmm_free = 0;
        return;
    }

    pmm_bitmap = (uint32_t*)bitmap_addr;
    /* mark all frames reserved initially */
    for (uint64_t i = 0; i < pmm_bitmap_words; ++i) pmm_bitmap[i] = 0xFFFFFFFFu;
    pmm_free = 0;

    /* release conventional regions described by firmware */
    if (boot && boot->magic == FOX_BOOT_INFO_MAGIC && boot->memory_map && boot->memory_map_count) {
        for (uint64_t i = 0; i < boot->memory_map_count; ++i) {
            const boot_memory_region_t* region = &boot->memory_map[i];
            if (region->type == BOOT_MEMORY_TYPE_CONVENTIONAL && region->page_count > 0) {
                uint64_t bytes = region->page_count * PAGE_SIZE;
                pmm_release_range((paddr_t)region->physical_start, bytes);
            }
        }
    }

    /* reserve the bitmap area itself so it isn't reused */
    pmm_reserve_range((paddr_t)bitmap_addr, (uint64_t)(pmm_bitmap_words * sizeof(uint32_t)));

    /* reserve kernel/loader/handoff regions if provided */
    if (boot && boot->magic == FOX_BOOT_INFO_MAGIC) {
        if (boot->kernel_base && boot->kernel_size) pmm_reserve_range((paddr_t)boot->kernel_base, (uint64_t)boot->kernel_size);
        if (boot->loader_base && boot->loader_size) pmm_reserve_range((paddr_t)boot->loader_base, (uint64_t)boot->loader_size);
        if (boot->handoff_base && boot->handoff_size) pmm_reserve_range((paddr_t)boot->handoff_base, (uint64_t)boot->handoff_size);
        /* reserve framebuffer area */
        if (boot->framebuffer.framebuffer_base && boot->framebuffer.framebuffer_size) pmm_reserve_range((paddr_t)boot->framebuffer.framebuffer_base, (uint64_t)boot->framebuffer.framebuffer_size);
    }

    /* ensure pmm_free is set if we cleared any bits via release_range */
    /* compute free count from bitmap */
    pmm_free = 0;
    for (uint64_t i = 0; i < pmm_total; ++i) {
        if (!frame_is_used(i)) pmm_free++;
    }

    /* initialize uc_table */
    for (uint32_t i = 0; i < MAX_UC; ++i) {
        uc_table[i].magic = 0;
        uc_table[i].total_chunks = 0;
        uc_table[i].used_bytes = 0;
    }
    heap_head = NULL;
}

uint64_t pmm_phys_end_bytes(void) { return pmm_phys_end; }

void setup_identity_paging(void) {
    serial_writeln("[mem] setup_identity_paging start");
    if (pmm_phys_end == 0 && pmm_total_conv_pages == 0) {
        serial_writeln("[mem] phys_end/conventional pages unknown, skipping paging");
        return;
    }

    uint64_t bytes;
    if (pmm_total_conv_pages > 0) bytes = pmm_total_conv_pages * PAGE_SIZE;
    else bytes = pmm_phys_end;

    /* compute number of PDs (each PD covers 1GiB via 512 * 2MiB entries) */
    uint64_t pd_count = (bytes + ((1ull<<30) - 1ull)) >> 30;
    if (pd_count == 0) pd_count = 1;
    if (pd_count > 512) pd_count = 512;

    uint64_t pages_needed = 2 + pd_count; /* PML4 + PDPT + pd_count PD pages */
    serial_write("[mem] paging: allocating pages for page-tables: "); serial_u64(pages_needed); serial_writeln("");
    paddr_t base = pmm_alloc_contiguous_pages(pages_needed);
    if (!base) { serial_writeln("[mem] paging: pmm_alloc_contiguous_pages failed"); return; }

    /* zero allocated pages */
    memzero((void*)(uintptr_t)base, (uint32_t)(pages_needed * PAGE_SIZE));

    uint64_t pml4_phys = base;
    uint64_t pdpt_phys = base + PAGE_SIZE;
    uint64_t pd_phys_base = base + (2 * PAGE_SIZE);

    /* set PML4[0] -> PDPT */
    uint64_t* pml4 = (uint64_t*)(uintptr_t)pml4_phys;
    uint64_t* pdpt = (uint64_t*)(uintptr_t)pdpt_phys;
    pml4[0] = pdpt_phys | 0x03u;

    /* set recursive mapping in PML4[511] -> PML4 */
    pml4[511] = pml4_phys | 0x03u;

    /* fill PDPT entries */
    for (uint64_t i = 0; i < pd_count; ++i) {
        pdpt[i] = (pd_phys_base + i * PAGE_SIZE) | 0x03u;
    }

    /* fill PDs with 2MiB pages */
    uint64_t page_index = 0;
    for (uint64_t i = 0; i < pd_count; ++i) {
        uint64_t* pd = (uint64_t*)(uintptr_t)(pd_phys_base + i * PAGE_SIZE);
        for (uint64_t e = 0; e < 512; ++e) {
            uint64_t phys = (page_index << 21); /* page_index * 2MiB */
            if (phys >= bytes) { pd[e] = 0; }
            else { pd[e] = phys | 0x83u; }
            page_index++;
        }
    }

    serial_writeln("[mem] paging: page-tables built; enabling paging now");

    /* Enable PAE in CR4 */
    unsigned long tmp;
    asm volatile ("mov %%cr4, %0" : "=r" (tmp));
    tmp |= (1ul << 5);
    asm volatile ("mov %0, %%cr4" :: "r" (tmp));

    /* Load CR3 with PML4 physical address */
    asm volatile ("mov %0, %%cr3" :: "r" (pml4_phys));

    /* Enable paging (CR0.PG) */
    asm volatile ("mov %%cr0, %0" : "=r" (tmp));
    tmp |= 0x80000000ul;
    asm volatile ("mov %0, %%cr0" :: "r" (tmp));

    serial_writeln("[mem] paging enabled");
}
paddr_t pmm_alloc_contiguous_pages(uint64_t pages) {    if (pages == 0 || pages > pmm_free || pages > pmm_total) return 0;

    uint64_t run = 0;
    uint64_t start = 0;
    for (uint64_t i = 0; i < pmm_total; ++i) {
        if (!frame_is_used(i)) {
            if (run == 0) start = i;
            run++;
            if (run == pages) {
                for (uint64_t j = 0; j < pages; ++j) frame_mark_used(start + j);
                return addr_for_frame_index(start);
            }
        } else {
            run = 0;
        }
    }
    return 0;
}

paddr_t pmm_alloc_frame(void) {
    return pmm_alloc_contiguous_pages(1);
}

void pmm_free_contiguous_pages(paddr_t addr, uint64_t pages) {
    if (!addr || pages == 0) return;
    if (addr < (paddr_t)pmm_base) return;
    uint64_t start = frame_index_for_addr(addr);
    if (start >= pmm_total) return;
    if (pages > pmm_total - start) pages = pmm_total - start;
    for (uint64_t i = 0; i < pages; ++i) frame_mark_free(start + i);
}

void pmm_free_frame(paddr_t addr) {
    pmm_free_contiguous_pages(addr, 1);
}

uint64_t pmm_total_pages(void) {
    return pmm_total;
}

uint64_t pmm_free_pages(void) {
    return pmm_free;
}

void* kmalloc(uint32_t size) {
    if (size == 0) return NULL;
    return heap_alloc(size);
}

void* kcalloc(uint32_t count, uint32_t size) {
    if (count == 0 || size == 0) return NULL;
    if (size > 0xFFFFFFFFu / count) return NULL;
    uint32_t total = count * size;
    void* ptr = kmalloc(total);
    if (ptr) memzero(ptr, total);
    return ptr;
}

void* krealloc(void* ptr, uint32_t size) {
    return heap_realloc_block(ptr, size);
}

void kfree(void* ptr) {
    heap_free_block(ptr);
}

uchandle_t uc_alloc(uint64_t bytes) {
    uint64_t need64 = (bytes + PAGE_SIZE - 1u) / PAGE_SIZE;
    uint32_t need = (need64 > 0xFFFFFFFFu) ? 0u : (uint32_t)need64;
    if (need == 0) need = 1;
    if (need > 1024u) return 0;

    ucdesc_t* d = NULL;
    for (uint32_t i = 0; i < MAX_UC; ++i) {
        if (uc_table[i].magic == 0) {
            d = &uc_table[i];
            break;
        }
    }
    if (!d) return 0;

    uint32_t allocated = 0;
    for (; allocated < need; ++allocated) {
        paddr_t addr = pmm_alloc_frame();
        if (!addr) break;
        d->chunk_idx[allocated] = (uint32_t)frame_index_for_addr(addr);
    }

    if (allocated != need) {
        for (uint32_t j = 0; j < allocated; ++j) {
            pmm_free_frame(frame_addr_from_chunk(d->chunk_idx[j]));
        }
        d->magic = 0;
        d->total_chunks = 0;
        d->used_bytes = 0;
        return 0;
    }

    d->total_chunks = need;
    d->used_bytes = 0;
    d->magic = (rnd32() | 1u);
    return d->magic;
}

int uc_free(uchandle_t h) {
    ucdesc_t* d = get_uc(h);
    if (!d) return -1;

    for (uint32_t k = 0; k < d->total_chunks; ++k) {
        pmm_free_frame(frame_addr_from_chunk(d->chunk_idx[k]));
        d->chunk_idx[k] = 0;
    }
    d->magic = 0;
    d->total_chunks = 0;
    d->used_bytes = 0;
    return 0;
}

uint64_t uc_size(uchandle_t h) {
    ucdesc_t* d = get_uc(h);
    return d ? (uint64_t)d->total_chunks * PAGE_SIZE : 0;
}

uint64_t uc_used(uchandle_t h) {
    ucdesc_t* d = get_uc(h);
    return d ? d->used_bytes : 0;
}

int uc_write(uchandle_t h, const void* src, uint64_t len) {
    ucdesc_t* d = get_uc(h);
    if (!d) return -1;
    uint64_t cap = (uint64_t)d->total_chunks * PAGE_SIZE;
    if (d->used_bytes > cap || len > cap - d->used_bytes) return -2;
    if (len > 0xFFFFFFFFu) return -3;

    const uint8_t* s = (const uint8_t*)src;
    uint64_t pos = d->used_bytes;
    while (len) {
        uint32_t cidx = pos / PAGE_SIZE;
        uint32_t off = pos % PAGE_SIZE;
        uint32_t space = PAGE_SIZE - off;
        uint32_t n = (len < (uint64_t)space) ? (uint32_t)len : space;
        uint8_t* dst = (uint8_t*)frame_addr_from_chunk(d->chunk_idx[cidx]) + off;
        memcopy(dst, s, n);
        s += n;
        pos += n;
        len -= n;
    }

    d->used_bytes = (uint64_t)pos;
    return 0;
}

int uc_read(uchandle_t h, uint64_t offset, void* dst, uint64_t len) {
    ucdesc_t* d = get_uc(h);
    if (!d) return -1;
    if (offset > d->used_bytes || len > d->used_bytes - offset) return -2;
    if (offset > 0xFFFFFFFFu || len > 0xFFFFFFFFu) return -3;

    uint8_t* out = (uint8_t*)dst;
    uint64_t pos = offset;
    uint64_t remain = len;
    while (remain) {
        uint32_t cidx = pos / PAGE_SIZE;
        uint32_t off = pos % PAGE_SIZE;
        uint32_t space = PAGE_SIZE - off;
        uint32_t n = (remain < (uint64_t)space) ? (uint32_t)remain : space;
        uint8_t* src = (uint8_t*)frame_addr_from_chunk(d->chunk_idx[cidx]) + off;
        memcopy(out, src, n);
        out += n;
        pos += n;
        remain -= n;
    }
    return 0;
}

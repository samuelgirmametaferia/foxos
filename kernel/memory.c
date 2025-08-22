#include <stdint.h>
#include <stddef.h>
#include "memory.h"

// Simple bump allocator over a fixed pool of chunks (identity-mapped physical)
static uint8_t pool[POOL_CHUNKS * CHUNK_SIZE];
static uint8_t chunk_used[POOL_CHUNKS];

// Unified chunk descriptor
typedef struct {
    uint32_t magic;          // simple capability check
    uint32_t total_chunks;   // allocated chunk count
    uint32_t used_bytes;     // bytes written via uc_write
    uint16_t chunk_idx[256]; // supports up to 256 chunks (1 MiB logical at 4 KiB)
} ucdesc_t;

#define MAX_UC 128
static ucdesc_t uc_table[MAX_UC];

static uint32_t rng_state = 0xC0FFEE01;
static uint32_t rnd32(void){ rng_state = rng_state*1664525u + 1013904223u; return rng_state; }

void mem_init(void) {
    for (uint32_t i = 0; i < POOL_CHUNKS; ++i) chunk_used[i] = 0;
    for (uint32_t i = 0; i < MAX_UC; ++i) uc_table[i].magic = 0;
}

static int alloc_chunk(void) {
    for (uint32_t i = 0; i < POOL_CHUNKS; ++i) {
        if (!chunk_used[i]) { chunk_used[i] = 1; return (int)i; }
    }
    return -1;
}

static void free_chunk(uint16_t idx) { if (idx < POOL_CHUNKS) chunk_used[idx] = 0; }

static ucdesc_t* get_uc(uchandle_t h) {
    if (h == 0) return NULL;
    ucdesc_t* d = &uc_table[h % MAX_UC];
    if (d->magic != h) return NULL;
    return d;
}

uchandle_t uc_alloc(uint32_t bytes) {
    uint32_t need = (bytes + CHUNK_SIZE - 1) / CHUNK_SIZE;
    if (need > 256) return 0;
    // find a free desc slot
    ucdesc_t* d = NULL; uint32_t slot = 0;
    for (uint32_t i = 0; i < MAX_UC; ++i) {
        if (uc_table[i].magic == 0) { d = &uc_table[i]; slot = i; break; }
    }
    if (!d) return 0;
    // allocate chunks
    for (uint32_t k = 0; k < need; ++k) {
        int idx = alloc_chunk();
        if (idx < 0) { // rollback
            for (uint32_t j = 0; j < k; ++j) free_chunk(d->chunk_idx[j]);
            return 0;
        }
        d->chunk_idx[k] = (uint16_t)idx;
    }
    d->total_chunks = need;
    d->used_bytes = 0;
    // generate capability handle
    uint32_t h = rnd32() | 1u;
    d->magic = h;
    return h;
}

int uc_free(uchandle_t h) {
    ucdesc_t* d = get_uc(h);
    if (!d) return -1;
    for (uint32_t k = 0; k < d->total_chunks; ++k) free_chunk(d->chunk_idx[k]);
    d->magic = 0;
    return 0;
}

static uint8_t* chunk_ptr(uint16_t idx) { return &pool[(uint32_t)idx * CHUNK_SIZE]; }

uint32_t uc_size(uchandle_t h) {
    ucdesc_t* d = get_uc(h);
    return d ? d->total_chunks * CHUNK_SIZE : 0;
}

uint32_t uc_used(uchandle_t h) {
    ucdesc_t* d = get_uc(h);
    return d ? d->used_bytes : 0;
}

int uc_write(uchandle_t h, const void* src, uint32_t len) {
    ucdesc_t* d = get_uc(h);
    if (!d) return -1;
    uint32_t cap = d->total_chunks * CHUNK_SIZE;
    if (d->used_bytes + len > cap) return -2;
    const uint8_t* s = (const uint8_t*)src;
    uint32_t pos = d->used_bytes;
    while (len) {
        uint32_t cidx = pos / CHUNK_SIZE;
        uint32_t off  = pos % CHUNK_SIZE;
        uint32_t space = CHUNK_SIZE - off;
        uint32_t n = (len < space) ? len : space;
        uint8_t* dst = chunk_ptr(d->chunk_idx[cidx]) + off;
        for (uint32_t i = 0; i < n; ++i) dst[i] = s[i];
        s += n; pos += n; len -= n;
    }
    d->used_bytes = pos;
    return 0;
}

int uc_read(uchandle_t h, uint32_t offset, void* dst, uint32_t len) {
    ucdesc_t* d = get_uc(h);
    if (!d) return -1;
    if (offset + len > d->used_bytes) return -2;
    uint8_t* out = (uint8_t*)dst;
    uint32_t pos = offset;
    while (len) {
        uint32_t cidx = pos / CHUNK_SIZE;
        uint32_t off  = pos % CHUNK_SIZE;
        uint32_t space = CHUNK_SIZE - off;
        uint32_t n = (len < space) ? len : space;
        uint8_t* src = chunk_ptr(d->chunk_idx[cidx]) + off;
        for (uint32_t i = 0; i < n; ++i) out[i] = src[i];
        out += n; pos += n; len -= n;
    }
    return 0;
}

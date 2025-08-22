#pragma once
#include <stdint.h>

#define CHUNK_SIZE 4096
#define POOL_CHUNKS 1024   // 4 MiB pool

void mem_init(void);

// Unified-chunk handle (capability). 0 means invalid.
typedef uint32_t uchandle_t;

uchandle_t uc_alloc(uint32_t bytes);
int uc_free(uchandle_t h);

// Append write: fills current chunk before using next
int uc_write(uchandle_t h, const void* src, uint32_t len);

// Random-access read
int uc_read(uchandle_t h, uint32_t offset, void* dst, uint32_t len);

// Query
uint32_t uc_size(uchandle_t h);      // capacity in bytes
uint32_t uc_used(uchandle_t h);      // bytes written via uc_write

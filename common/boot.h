#pragma once
#include <stdint.h>

#define FOX_BOOT_INFO_MAGIC 0x314F4F42584F4600ull
#define FOX_BOOT_INFO_VERSION 1ull

#define BOOT_MEMORY_TYPE_RESERVED 0ull
#define BOOT_MEMORY_TYPE_LOADER_CODE 1ull
#define BOOT_MEMORY_TYPE_LOADER_DATA 2ull
#define BOOT_MEMORY_TYPE_BOOT_SERVICES_CODE 3ull
#define BOOT_MEMORY_TYPE_BOOT_SERVICES_DATA 4ull
#define BOOT_MEMORY_TYPE_CONVENTIONAL 7ull
#define BOOT_MEMORY_TYPE_ACPI_RECLAIMABLE 9ull
#define BOOT_MEMORY_TYPE_ACPI_NVS 10ull

typedef struct {
    uint64_t type;
    uint64_t physical_start;
    uint64_t virtual_start;
    uint64_t page_count;
    uint64_t attribute;
} boot_memory_region_t;

typedef struct {
    uint64_t framebuffer_base;
    uint64_t framebuffer_size;
    uint32_t width;
    uint32_t height;
    uint32_t pixels_per_scanline;
    uint32_t pixel_format;
    uint32_t red_mask;
    uint32_t green_mask;
    uint32_t blue_mask;
    uint32_t reserved;
} boot_framebuffer_t;

typedef struct {
    uint64_t magic;
    uint64_t version;
    uint64_t loader_base;
    uint64_t loader_size;
    uint64_t kernel_base;
    uint64_t kernel_size;
    uint64_t handoff_base;
    uint64_t handoff_size;
    uint64_t memory_map_count;
    const boot_memory_region_t* memory_map;
    boot_framebuffer_t framebuffer;
} boot_info_t;
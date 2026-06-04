#include "gpu.h"
#include "serial.h"
#include "../fs/devfs.h"
#include "../kernel/memory.h"

static uint64_t fb_base = 0;
static uint64_t fb_size = 0;
static uint32_t fb_width = 0;
static uint32_t fb_height = 0;
static uint32_t fb_stride = 0;

static void* devfs_fb_mmap(uint64_t length, uint64_t offset) {
    serial_write("[gpu] mmap request len="); serial_u64(length);
    serial_write(" off="); serial_u64(offset);
    serial_write(" fb_size="); serial_u64(fb_size);
    serial_writeln("");
    
    if (offset >= fb_size || offset + length > fb_size) {
        serial_writeln("[gpu] mmap failed: bounds check");
        return (void*)-1;
    }
    return (void*)(uintptr_t)(fb_base + offset);
}

static devfs_ops_t fb_ops = {
    .read = 0,
    .write = 0,
    .ioctl = 0,
    .mmap = devfs_fb_mmap
};

void gpu_init(const boot_info_t* boot) {
    if (boot && boot->magic == FOX_BOOT_INFO_MAGIC) {
        fb_base = boot->framebuffer.framebuffer_base;
        fb_size = boot->framebuffer.framebuffer_size;
        fb_width = boot->framebuffer.width;
        fb_height = boot->framebuffer.height;
        fb_stride = boot->framebuffer.pixels_per_scanline ? boot->framebuffer.pixels_per_scanline : fb_width;
        
        if (fb_base != 0 && fb_size != 0) {
            devfs_register("fb0", &fb_ops);
        }
    }
}

uint32_t gpu_get_width(void) { return fb_width; }
uint32_t gpu_get_height(void) { return fb_height; }
uint32_t gpu_get_stride(void) { return fb_stride ? fb_stride : fb_width; }

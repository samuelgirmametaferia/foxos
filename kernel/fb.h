#pragma once
#include <stdint.h>

typedef struct {
    uint32_t magic;     // 0xB007F00D
    uint16_t present;   // 1 if VBE LFB set
    uint16_t reserved;
    uint16_t width;
    uint16_t height;
    uint16_t pitch;     // bytes per scanline
    uint8_t  bpp;       // 16 or 32
    uint8_t  pad[1];
    uint32_t phys_base; // LFB physical address
} bootinfo_fb_t;

#define FB_BOOTINFO_ADDR 0x00070000u

typedef struct {
    uint8_t present;     // 1 if LFB available
    uint16_t width;
    uint16_t height;
    uint16_t pitch;      // bytes per scanline
    uint8_t bpp;         // bits per pixel (16 or 32 supported)
    volatile void* addr; // linear framebuffer base
} framebuffer_t;

extern framebuffer_t g_fb;

void fb_init(void);
uint16_t fb_rgb565(uint8_t r, uint8_t g, uint8_t b);
void fb_clear(uint32_t color);
void fb_putpixel(int x, int y, uint32_t color);
void fb_fill_rect(int x, int y, int w, int h, uint32_t color);
void fb_rect_border(int x, int y, int w, int h, int thickness, uint32_t color);

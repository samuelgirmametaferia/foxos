#include <stdint.h>
#include "fb.h"
#include "console.h"
#include "io.h"

framebuffer_t g_fb = {0};

void fb_init(void){
    const bootinfo_fb_t* bi = (const bootinfo_fb_t*)(uintptr_t)FB_BOOTINFO_ADDR;
    if (bi && bi->magic == 0xB007F00Du && bi->present){
        g_fb.present = 1;
        g_fb.width = bi->width;
        g_fb.height = bi->height;
        g_fb.pitch = bi->pitch;
        g_fb.bpp = bi->bpp;
        g_fb.addr = (volatile void*)(uintptr_t)bi->phys_base;
        console_writeln("fb: initialized from boot info");
        return;
    }
    g_fb.present = 0;
    // Do not attempt any graphics changes here; stay in text mode
    console_writeln("fb: no LFB (staying in text mode)");
}

uint16_t fb_rgb565(uint8_t r, uint8_t g, uint8_t b){
    return (uint16_t)(((r>>3)<<11) | ((g>>2)<<5) | (b>>3));
}

void fb_clear(uint32_t color){
    if(!g_fb.present) return;
    uint8_t* base = (uint8_t*)g_fb.addr;
    if (g_fb.bpp == 32){
        for (int y=0; y<g_fb.height; ++y){
            uint32_t* row = (uint32_t*)(base + y*g_fb.pitch);
            for (int x=0; x<g_fb.width; ++x) row[x] = color;
        }
    } else if (g_fb.bpp == 16){
        uint16_t c = (uint16_t)color;
        for (int y=0; y<g_fb.height; ++y){
            uint16_t* row = (uint16_t*)(base + y*g_fb.pitch);
            for (int x=0; x<g_fb.width; ++x) row[x] = c;
        }
    }
}

void fb_putpixel(int x, int y, uint32_t color){
    if(!g_fb.present) return; if (x<0||y<0||x>=g_fb.width||y>=g_fb.height) return;
    uint8_t* base = (uint8_t*)g_fb.addr;
    if (g_fb.bpp == 32){
        *(uint32_t*)(base + y*g_fb.pitch + x*4) = color;
    } else if (g_fb.bpp == 16){
        *(uint16_t*)(base + y*g_fb.pitch + x*2) = (uint16_t)color;
    }
}

void fb_fill_rect(int x, int y, int w, int h, uint32_t color){
    if(!g_fb.present) return;
    if (x<0){ w+=x; x=0; } if (y<0){ h+=y; y=0; }
    if (x+w>g_fb.width) w=g_fb.width-x; if (y+h>g_fb.height) h=g_fb.height-y;
    if (w<=0||h<=0) return;
    uint8_t* base = (uint8_t*)g_fb.addr;
    if (g_fb.bpp == 32){
        for (int yy=0; yy<h; ++yy){
            uint32_t* row = (uint32_t*)(base + (y+yy)*g_fb.pitch + x*4);
            for (int xx=0; xx<w; ++xx) row[xx] = color;
        }
    } else if (g_fb.bpp == 16){
        uint16_t c=(uint16_t)color;
        for (int yy=0; yy<h; ++yy){
            uint16_t* row = (uint16_t*)(base + (y+yy)*g_fb.pitch + x*2);
            for (int xx=0; xx<w; ++xx) row[xx] = c;
        }
    }
}

void fb_rect_border(int x, int y, int w, int h, int thickness, uint32_t color){
    if(!g_fb.present) return;
    fb_fill_rect(x, y, w, thickness, color);
    fb_fill_rect(x, y+h-thickness, w, thickness, color);
    fb_fill_rect(x, y, thickness, h, color);
    fb_fill_rect(x+w-thickness, y, thickness, h, color);
}

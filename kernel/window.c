#include <stdint.h>
#include "window.h"

#define VGA_MEM ((volatile uint16_t*)0xB8000)
#define VGA_COLS 80
#define VGA_ROWS 25

static inline uint16_t vga_cell(uint8_t fg, uint8_t bg, char c){ return (uint16_t)(((bg<<4)|(fg&0x0F))<<8) | (uint8_t)c; }

void window_init(Window* win, int x, int y, int w, int h, const char* title, uint8_t fg, uint8_t bg){
    win->x=x; win->y=y; win->w=w; win->h=h; win->cx=0; win->cy=0; win->fg=fg; win->bg=bg;
    int i=0; for(; title && title[i] && i<(int)sizeof(win->title)-1; ++i) win->title[i]=title[i]; win->title[i]=0;
}

void window_draw(const Window* win){
    // border + title bar
    uint8_t tb_fg = 0x0F, tb_bg = 0x01; // white on blue title bar
    for(int yy=0; yy<win->h; ++yy){
        int gy = win->y + yy; if (gy<0 || gy>=VGA_ROWS) continue;
        for(int xx=0; xx<win->w; ++xx){
            int gx = win->x + xx; if (gx<0 || gx>=VGA_COLS) continue;
            uint8_t fg = win->fg, bg = win->bg; char ch=' ';
            // top/bottom border
            if (yy==0 || yy==win->h-1 || xx==0 || xx==win->w-1){ fg = 0x0F; bg = 0x00; ch = (yy==0||yy==win->h-1) ? '-' : '|'; }
            // title bar overwrite top interior
            if (yy==0){ bg = tb_bg; fg = tb_fg; ch=' '; }
            VGA_MEM[gy*VGA_COLS + gx] = vga_cell(fg,bg,ch);
        }
    }
    // write title centered on title bar
    if (win->title[0]){
        int len=0; while(win->title[len] && len<60) len++;
        int start = win->x + (win->w - len)/2; if (start < win->x+1) start = win->x+1;
        for (int i=0; i<len && (start+i) < (win->x + win->w - 1); ++i){
            int gx = start + i; int gy = win->y + 0;
            VGA_MEM[gy*VGA_COLS + gx] = vga_cell(0x0F, 0x01, win->title[i]);
        }
    }
}

static void put_at(int gx, int gy, uint8_t fg, uint8_t bg, char c){
    if (gx<0||gx>=VGA_COLS||gy<0||gy>=VGA_ROWS) return; VGA_MEM[gy*VGA_COLS + gx] = vga_cell(fg,bg,c);
}

void window_clear_client(const Window* win){
    for(int yy=1; yy<win->h-1; ++yy){
        for(int xx=1; xx<win->w-1; ++xx){ put_at(win->x+xx, win->y+yy, win->fg, win->bg, ' '); }
    }
}

void window_putc(Window* win, char c){
    int maxw = win->w - 2; int maxh = win->h - 2; if (maxw<=0||maxh<=0) return;
    if (c=='\n'){ win->cx=0; win->cy++; }
    else if (c=='\r'){ win->cx=0; }
    else if (c=='\b'){ if (win->cx>0){ win->cx--; put_at(win->x+1+win->cx, win->y+1+win->cy, win->fg, win->bg, ' ');} }
    else { put_at(win->x+1+win->cx, win->y+1+win->cy, win->fg, win->bg, c); if(++win->cx>=maxw){ win->cx=0; win->cy++; } }
    if (win->cy>=maxh){ // simple scroll within window
        for(int yy=0; yy<maxh-1; ++yy){
            for(int xx=0; xx<maxw; ++xx){
                int srcx=win->x+1+xx, srcy=win->y+1+yy+1;
                int dstx=win->x+1+xx, dsty=win->y+1+yy;
                VGA_MEM[dsty*VGA_COLS + dstx] = VGA_MEM[srcy*VGA_COLS + srcx];
            }
        }
        for(int xx=0; xx<maxw; ++xx){ put_at(win->x+1+xx, win->y+1+maxh-1, win->fg, win->bg, ' '); }
        win->cy = maxh-1;
    }
}

void window_write(Window* win, const char* s){ while(*s) window_putc(win, *s++); }
void window_writeln(Window* win, const char* s){ window_write(win,s); window_putc(win,'\n'); }

#include <stdint.h>
#include "console.h"
#include "io.h"

#define VGA_MEM ((volatile uint16_t*)0xB8000)
#define VGA_COLS 80
#define VGA_ROWS 25

static int cx = 0, cy = 0;
static uint8_t color = 0x0F; // white on black

static inline uint16_t vga_entry(char c) {
    return ((uint16_t)color << 8) | (uint8_t)c;
}

static void vga_hide_cursor(void){
    // Disable hardware cursor (bit 5 in Cursor Start register)
    outb(0x3D4, 0x0A);
    outb(0x3D5, 0x20);
}

static void scroll_if_needed(void) {
    if (cy < VGA_ROWS) return;
    // scroll up by 1 line
    for (int y = 1; y < VGA_ROWS; ++y) {
        for (int x = 0; x < VGA_COLS; ++x) {
            VGA_MEM[(y-1)*VGA_COLS + x] = VGA_MEM[y*VGA_COLS + x];
        }
    }
    // clear last line
    for (int x = 0; x < VGA_COLS; ++x) VGA_MEM[(VGA_ROWS-1)*VGA_COLS + x] = ((uint16_t)color << 8) | ' ';
    cy = VGA_ROWS - 1;
}

void console_init(void) {
    console_clear();
    vga_hide_cursor();
}

void console_clear(void) {
    cx = cy = 0;
    for (int i = 0; i < VGA_COLS*VGA_ROWS; ++i) VGA_MEM[i] = ((uint16_t)color << 8) | ' ';
}

void console_set_color(uint8_t fg, uint8_t bg) {
    color = (bg << 4) | (fg & 0x0F);
}

void console_putc(char c) {
    if (c == '\n') {
        cx = 0; cy++;
        scroll_if_needed();
        return;
    }
    if (c == '\r') { cx = 0; return; }
    if (c == '\b') {
        if (cx > 0) { cx--; VGA_MEM[cy*VGA_COLS + cx] = vga_entry(' '); }
        return;
    }
    VGA_MEM[cy*VGA_COLS + cx] = vga_entry(c);
    if (++cx >= VGA_COLS) { cx = 0; cy++; scroll_if_needed(); }
}

void console_write(const char* s) {
    while (*s) console_putc(*s++);
}

void console_writeln(const char* s) {
    console_write(s);
    console_putc('\n');
}

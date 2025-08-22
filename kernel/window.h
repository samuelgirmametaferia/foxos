#pragma once
#include <stdint.h>

typedef struct {
    int x, y, w, h;      // outer rect including border
    int cx, cy;          // cursor in client area (relative to client)
    uint8_t fg, bg;      // colors
    char title[64];
} Window;

void window_init(Window* win, int x, int y, int w, int h, const char* title, uint8_t fg, uint8_t bg);
void window_draw(const Window* win);
void window_clear_client(const Window* win);
void window_putc(Window* win, char c);
void window_write(Window* win, const char* s);
void window_writeln(Window* win, const char* s);

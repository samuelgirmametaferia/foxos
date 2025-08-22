#pragma once
#include <stdint.h>
#include "fb.h"

typedef struct {
    int x, y, w, h;
    uint32_t bg;
    uint32_t border;
    uint32_t title_bg;
    uint32_t title_fg;
    char title[64];
} GuiWindow;

void gui_init(void);
void gui_window_init(GuiWindow* w, int x, int y, int w_, int h_, const char* title);
void gui_window_draw(const GuiWindow* w);
void gui_window_fill_text(const GuiWindow* w, const char* text);

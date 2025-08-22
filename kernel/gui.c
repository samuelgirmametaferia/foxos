#include <stdint.h>
#include "gui.h"
#include "console.h"

void gui_init(void){
#ifndef ENABLE_GUI
    // GUI disabled; do nothing
    console_writeln("gui: disabled");
    return;
#else
    fb_init();
    if (!g_fb.present){
        console_writeln("gui: framebuffer not available; staying in text mode");
    } else {
        fb_clear(0x00202020); // dark gray
    }
#endif
}

void gui_window_init(GuiWindow* w, int x, int y, int w_, int h_, const char* title){
    w->x=x; w->y=y; w->w=w_; w->h=h_;
    w->bg = 0x00C0C0C0;      // light gray
    w->border = 0x00404040;  // dark border
    w->title_bg = 0x00000080; // navy
    w->title_fg = 0x00FFFFFF; // white
    int i=0; for(; title && title[i] && i<(int)sizeof(w->title)-1; ++i) w->title[i]=title[i]; w->title[i]=0;
}

static void draw_char(int x, int y, uint32_t fg, uint32_t bg, char c){
    // Placeholder: no font yet; draw a tiny 6x8 block for each char to visualize text
    int cw=6, ch=8;
    fb_fill_rect(x, y, cw, ch, bg);
    // Basic bar for non-space
    if (c!=' '){ fb_rect_border(x, y, cw, ch, 1, fg); }
}

void gui_window_draw(const GuiWindow* w){
#ifndef ENABLE_GUI
    return;
#else
    if (!g_fb.present) return;
    // body and border
    fb_fill_rect(w->x, w->y, w->w, w->h, w->bg);
    fb_rect_border(w->x, w->y, w->w, w->h, 2, w->border);
    // title bar
    int tb_h = 16;
    fb_fill_rect(w->x+2, w->y+2, w->w-4, tb_h, w->title_bg);
    // title text mock (monospace 6x8 blocks)
    int tx = w->x+6, ty = w->y+4;
    for(int i=0; w->title[i] && i<50; ++i){ draw_char(tx+i*6, ty, w->title_fg, w->title_bg, w->title[i]); }
#endif
}

void gui_window_fill_text(const GuiWindow* w, const char* text){
#ifndef ENABLE_GUI
    return;
#else
    if (!g_fb.present) return;
    int tx = w->x+6, ty = w->y+20; int col=0, row=0; int cw=6, ch=8; int maxcols=(w->w-12)/cw; int maxrows=(w->h-28)/ch;
    for (int i=0; text[i] && row<maxrows; ++i){
        char c=text[i];
        if (c=='\n'){ col=0; row++; continue; }
        draw_char(tx + col*cw, ty + row*ch, 0x00000000, w->bg, c);
        if (++col>=maxcols){ col=0; row++; }
    }
#endif
}

#include "syscalls.h"
#include "font.h"

uint32_t* fb;
int WIDTH = 1280;
int HEIGHT = 800;

static void log_line(const char* s) {
    uint64_t n = 0;
    while (s[n]) n++;
    sys_write(1, s, n);
    sys_write(1, "\n", 1);
}

// Colors (Solarized-inspired)
#define COL_BG      0x002B36u
#define COL_FG      0x839496u
#define COL_ACCENT  0x268BD2u
#define COL_WHITE   0xFFFFFFu
#define COL_BLACK   0x000000u
#define COL_ORANGE  0xCB4B16u

static void draw_rect(int x, int y, int w, int h, uint32_t col) {
    for (int i = 0; i < h; i++) {
        for (int j = 0; j < w; j++) {
            if (x + j < WIDTH && y + i < HEIGHT) {
                fb[(y + i) * WIDTH + (x + j)] = col;
            }
        }
    }
}

static void draw_char(int x, int y, char c, uint32_t col) {
    if (c < 32 || c > 126) return;
    const uint8_t* glyph = font8x8_basic[(int)c];
    for (int i = 0; i < 8; i++) {
        for (int j = 0; j < 8; j++) {
            if (glyph[i] & (1 << j)) {
                draw_rect(x + j*2, y + i*2, 2, 2, col);
            }
        }
    }
}

static void draw_string(int x, int y, const char* s, uint32_t col) {
    while (*s) {
        draw_char(x, y, *s++, col);
        x += 18;
    }
}

static void draw_fox(int x, int y, int scale) {
    // Ear L
    draw_rect(x + 2*scale, y, 4*scale, 4*scale, COL_ORANGE);
    // Ear R
    draw_rect(x + 10*scale, y, 4*scale, 4*scale, COL_ORANGE);
    // Face
    draw_rect(x, y + 4*scale, 16*scale, 10*scale, COL_ORANGE);
    // Eyes
    draw_rect(x + 3*scale, y + 6*scale, 2*scale, 2*scale, COL_WHITE);
    draw_rect(x + 11*scale, y + 6*scale, 2*scale, 2*scale, COL_WHITE);
    draw_rect(x + 4*scale, y + 7*scale, 1*scale, 1*scale, COL_BLACK);
    draw_rect(x + 12*scale, y + 7*scale, 1*scale, 1*scale, COL_BLACK);
    // Nose
    draw_rect(x + 7*scale, y + 10*scale, 2*scale, 2*scale, COL_BLACK);
}

typedef enum {
    TAB_SYSTEM,
    TAB_FILES,
    TAB_CONSOLE
} tab_t;

tab_t current_tab = TAB_SYSTEM;
int cursor_visible = 1;
uint64_t last_cursor_toggle = 0;

char cmd_buf[128];
int cmd_len = 0;

char history[16][128];
int history_count = 0;
int history_index = -1;

void draw_system_tab() {
    sys_info_t info;
    if (sys_getinfo(&info) == 0) {
        char buf[64];
        draw_string(100, 200, "SYSTEM INFORMATION", COL_ACCENT);
        
        draw_string(100, 250, "Cores: ", COL_FG);
        buf[0] = '0' + (info.cpu_count % 10); buf[1] = 0;
        draw_string(300, 250, buf, COL_WHITE);
        
        draw_string(100, 300, "Memory: ", COL_FG);
        draw_string(300, 300, "Total RAM detected", COL_WHITE);
        
        draw_string(100, 350, "Processes: ", COL_FG);
        buf[0] = '0' + (info.process_count % 10); buf[1] = 0;
        draw_string(300, 350, buf, COL_WHITE);
        
        draw_string(100, 400, "Uptime: ", COL_FG);
        draw_string(300, 400, "Active", COL_WHITE);
    }
}

void draw_files_tab() {
    draw_string(100, 200, "FILE EXPLORER", COL_ACCENT);
    draw_string(100, 250, "Mount: / (foxFS)", COL_FG);
    draw_rect(100, 280, WIDTH - 200, 2, COL_FG);
    draw_string(120, 310, "[D] bin", COL_WHITE);
    draw_string(120, 340, "[D] dev", COL_WHITE);
    draw_string(120, 370, "[F] kernel.elf", COL_WHITE);
    draw_string(120, 400, "[F] initrd.img", COL_WHITE);
}

void draw_console_tab() {
    draw_rect(80, 180, WIDTH - 160, HEIGHT - 300, COL_BLACK);
    draw_string(100, 200, "foxOS Terminal v1.0", COL_ACCENT);
    draw_string(100, 240, "root@foxos:/# ", COL_FG);
    draw_string(340, 240, cmd_buf, COL_WHITE);
    
    if (cursor_visible) {
        draw_rect(340 + cmd_len * 18, 240, 12, 16, COL_FG);
    }
    
    for (int i = 0; i < 5 && i < history_count; i++) {
        draw_string(100, 300 + i*30, history[(history_count - 1 - i) % 16], COL_FG);
    }
}

int main(void) {
    sys_info_t info;
    if (sys_getinfo(&info) == 0) {
        WIDTH = info.fb_width;
        HEIGHT = info.fb_height;
    }

    log_line("[tss] started");

    int fbfd = sys_open("/dev/fb0", 0, 0);
    fb = (uint32_t*)sys_mmap(fbfd, (uint64_t)WIDTH * HEIGHT * 4, 0);
    int kbd = sys_open("/dev/kbd", 0, 0);

    int use_fb = 1;
    if (fb == (uint32_t*)-1) {
        use_fb = 0;
        fb = (uint32_t*)0;
        log_line("[tss] fb mmap failed, falling back to serial console");
    }

    if (!use_fb) {
        /* Serial-only fallback: echo keys to stdout so the OS remains usable */
        while (1) {
            char c;
            if (kbd >= 0 && sys_read(kbd, &c, 1) == 1) {
                sys_write(1, &c, 1);
            } else {
                sys_sleep(16);
            }
        }
    }

    while (1) {
        draw_rect(0, 0, WIDTH, HEIGHT, COL_BG);
        draw_rect(0, 0, WIDTH, 80, COL_BLACK);
        draw_fox(20, 15, 3);
        draw_string(100, 30, "foxOS - THE SYSTEM SHELL", COL_ORANGE);
        
        draw_rect(WIDTH - 600, 0, 600, 80, 0x073642u);
        draw_string(WIDTH - 580, 30, "[ SYSTEM ]", current_tab == TAB_SYSTEM ? COL_WHITE : COL_FG);
        draw_string(WIDTH - 380, 30, "[ FILES ]", current_tab == TAB_FILES ? COL_WHITE : COL_FG);
        draw_string(WIDTH - 180, 30, "[ CONSOLE ]", current_tab == TAB_CONSOLE ? COL_WHITE : COL_FG);
        
        static int animated_tab_x = -1;
        int target_tab_x = WIDTH - 580 + current_tab * 200;
        if (animated_tab_x == -1) animated_tab_x = target_tab_x;
        if (animated_tab_x < target_tab_x) animated_tab_x += 10;
        if (animated_tab_x > target_tab_x) animated_tab_x -= 10;
        draw_rect(animated_tab_x, 70, 150, 5, COL_ACCENT);
        
        switch (current_tab) {
            case TAB_SYSTEM: draw_system_tab(); break;
            case TAB_FILES: draw_files_tab(); break;
            case TAB_CONSOLE: draw_console_tab(); break;
        }
        
        char c;
        if (sys_read(kbd, &c, 1) == 1) {
            if (c == '\t') {
                log_line("[tss] tab");
                current_tab = (current_tab + 1) % 3;
            } else if (current_tab == TAB_CONSOLE) {
                if (c == '\n') {
                    if (cmd_len > 0) {
                        int is_clear = (cmd_buf[0]=='c' && cmd_buf[1]=='l' && cmd_buf[2]=='e' && cmd_buf[3]=='a' && cmd_buf[4]=='r' && cmd_buf[5]==0);
                        if (is_clear) history_count = 0;
                        else {
                            for(int i=0; i<128; i++) history[history_count % 16][i] = cmd_buf[i];
                            history_count++;
                        }
                        cmd_buf[0] = 0; cmd_len = 0;
                    }
                } else if (c == '\b') {
                    if (cmd_len > 0) cmd_buf[--cmd_len] = 0;
                } else if (cmd_len < 127 && c >= 32 && c <= 126) {
                    cmd_buf[cmd_len++] = c;
                    cmd_buf[cmd_len] = 0;
                }
            }
        }
        
        sys_info_t info;
        sys_getinfo(&info);
        if (info.uptime_ms - last_cursor_toggle > 500) {
            cursor_visible = !cursor_visible;
            last_cursor_toggle = info.uptime_ms;
        }
        sys_sleep(16); // ~60 FPS cap to prevent CPU hogging
    }
    return 0;
}

void _start(void) {
    main();
    sys_exit(0);
}

#include "syscalls.h"
#include "font.h"

volatile uint32_t* fb;
int WIDTH = 1280;
int HEIGHT = 800;
int STRIDE = 1280;

// Double buffering - support up to 2048x1280
#define BUF_WIDTH 2048
#define BUF_HEIGHT 1280
static uint32_t back_buffer[BUF_WIDTH * BUF_HEIGHT];

// Mouse state
int mouse_x = 640;
int mouse_y = 400;
uint32_t mouse_buttons = 0;

typedef struct {
    int dx;
    int dy;
    uint32_t buttons;
} mouse_event_t;

static void log_line(const char* s) {
    uint64_t n = 0;
    while (s[n]) n++;
    sys_write(1, s, n);
    sys_write(1, "\n", 1);
}

// Colors
#define COL_BG      0x002B36u
#define COL_FG      0x839496u
#define COL_ACCENT  0x268BD2u
#define COL_WHITE   0xFFFFFFu
#define COL_BLACK   0x000000u
#define COL_ORANGE  0xCB4B16u
#define COL_GREY    0x586E75u

static void flip(void) {
    if (!fb || fb == (uint32_t*)-1) return;
    for (int i = 0; i < HEIGHT && i < BUF_HEIGHT; i++) {
        for (int j = 0; j < WIDTH && j < BUF_WIDTH; j++) {
            fb[i * STRIDE + j] = back_buffer[i * BUF_WIDTH + j];
        }
    }
}

static uint32_t alpha_blend(uint32_t bg, uint32_t fg, uint8_t alpha) {
    uint32_t a = alpha;
    uint32_t ia = 255 - a;
    
    uint32_t rb = (bg >> 16) & 0xFF;
    uint32_t gb = (bg >> 8) & 0xFF;
    uint32_t bb = bg & 0xFF;
    
    uint32_t rf = (fg >> 16) & 0xFF;
    uint32_t gf = (fg >> 8) & 0xFF;
    uint32_t bf = fg & 0xFF;
    
    uint32_t r = (rb * ia + rf * a) >> 8;
    uint32_t g = (gb * ia + gf * a) >> 8;
    uint32_t b = (bb * ia + bf * a) >> 8;
    
    return (r << 16) | (g << 8) | b;
}

static void draw_rect(int x, int y, int w, int h, uint32_t col) {
    for (int i = 0; i < h; i++) {
        for (int j = 0; j < w; j++) {
            if (x + j >= 0 && x + j < BUF_WIDTH && y + i >= 0 && y + i < BUF_HEIGHT) {
                back_buffer[(y + i) * BUF_WIDTH + (x + j)] = col;
            }
        }
    }
}

static void draw_rect_alpha(int x, int y, int w, int h, uint32_t col, uint8_t alpha) {
    for (int i = 0; i < h; i++) {
        for (int j = 0; j < w; j++) {
            if (x + j >= 0 && x + j < BUF_WIDTH && y + i >= 0 && y + i < BUF_HEIGHT) {
                uint32_t bg = back_buffer[(y + i) * BUF_WIDTH + (x + j)];
                back_buffer[(y + i) * BUF_WIDTH + (x + j)] = alpha_blend(bg, col, alpha);
            }
        }
    }
}

static void draw_gradient(int x, int y, int w, int h, uint32_t c1, uint32_t c2) {
    for (int i = 0; i < h; i++) {
        uint32_t col = alpha_blend(c1, c2, (i * 255) / h);
        for (int j = 0; j < w; j++) {
            if (x + j >= 0 && x + j < BUF_WIDTH && y + i >= 0 && y + i < BUF_HEIGHT) {
                back_buffer[(y + i) * BUF_WIDTH + (x + j)] = col;
            }
        }
    }
}

static void draw_char(int x, int y, char c, uint32_t col) {
    if (c < 32 || c > 126) return;
    const uint8_t* glyph = font8x8_basic[(int)c];
    for (int i = 0; i < 8; i++) {
        for (int j = 0; j < 8; j++) {
            if (glyph[i] & (1 << (7 - j))) {
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

static void draw_cursor(int x, int y) {
    // Stylish modern arrow cursor
    for (int i = 0; i < 15; i++) {
        for (int j = 0; j < i; j++) {
            draw_rect(x + j, y + i, 1, 1, COL_WHITE);
        }
    }
    // Border
    for (int i = 0; i < 16; i++) {
        draw_rect(x + i, y + i, 1, 1, COL_BLACK);
        draw_rect(x, y + i, 1, 1, COL_BLACK);
    }
    for (int j = 0; j < 16; j++) {
        draw_rect(x + j, y + 15, 1, 1, COL_BLACK);
    }
}

static void draw_fox(int x, int y, int scale) {
    draw_rect(x + 2*scale, y, 4*scale, 4*scale, COL_ORANGE);
    draw_rect(x + 10*scale, y, 4*scale, 4*scale, COL_ORANGE);
    draw_rect(x, y + 4*scale, 16*scale, 10*scale, COL_ORANGE);
    draw_rect(x + 3*scale, y + 6*scale, 2*scale, 2*scale, COL_WHITE);
    draw_rect(x + 11*scale, y + 6*scale, 2*scale, 2*scale, COL_WHITE);
    draw_rect(x + 4*scale, y + 7*scale, 1*scale, 1*scale, COL_BLACK);
    draw_rect(x + 12*scale, y + 7*scale, 1*scale, 1*scale, COL_BLACK);
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

void draw_system_tab() {
    sys_info_t info;
    if (sys_getinfo(&info) == 0) {
        char buf[64];
        draw_rect_alpha(80, 150, WIDTH - 160, 400, 0x073642u, 180);
        draw_string(120, 200, "SYSTEM STATUS", COL_ACCENT);
        
        draw_string(120, 260, "Cores Detected: ", COL_FG);
        buf[0] = '0' + (info.cpu_count % 10); buf[1] = 0;
        draw_string(450, 260, buf, COL_WHITE);
        
        draw_string(120, 310, "Physical Memory: ", COL_FG);
        draw_string(450, 310, "Healthy", COL_WHITE);
        
        draw_string(120, 360, "Active Processes: ", COL_FG);
        buf[0] = '0' + (info.process_count % 10); buf[1] = 0;
        draw_string(450, 360, buf, COL_WHITE);
        
        draw_string(120, 410, "Kernel Uptime: ", COL_FG);
        draw_string(450, 410, "Running", COL_WHITE);
    }
}

void draw_files_tab() {
    draw_rect_alpha(80, 150, WIDTH - 160, 400, 0x073642u, 180);
    draw_string(120, 200, "FILE EXPLORER", COL_ACCENT);
    draw_string(120, 250, "Mount: / (root)", COL_FG);
    draw_rect(120, 280, WIDTH - 240, 2, COL_GREY);
    draw_string(140, 310, "[D] bin", COL_WHITE);
    draw_string(140, 340, "[D] dev", COL_WHITE);
    draw_string(140, 370, "[F] kernel.elf", COL_WHITE);
    draw_string(140, 400, "[F] initrd.fx", COL_WHITE);
}

void draw_console_tab() {
    draw_rect_alpha(80, 150, WIDTH - 160, HEIGHT - 300, COL_BLACK, 200);
    draw_string(120, 180, "foxOS Terminal", COL_ACCENT);
    draw_string(120, 220, "root@foxos:/# ", COL_FG);
    draw_string(360, 220, cmd_buf, COL_WHITE);
    
    if (cursor_visible) {
        draw_rect(360 + cmd_len * 18, 220, 12, 16, COL_ACCENT);
    }
    
    for (int i = 0; i < 8 && i < history_count; i++) {
        draw_string(120, 280 + i*30, history[(history_count - 1 - i) % 16], COL_FG);
    }
}

static int abs(int x) { return x < 0 ? -x : x; }

int main(void) {
    sys_info_t info;
    if (sys_getinfo(&info) == 0) {
        WIDTH = info.fb_width;
        HEIGHT = info.fb_height;
        STRIDE = info.fb_stride;
    }

    int fbfd = sys_open("/dev/fb0", 0, 0);
    fb = (uint32_t*)sys_mmap(fbfd, (uint64_t)WIDTH * HEIGHT * 4, 0);
    int kbd = sys_open("/dev/kbd", 0, 0);
    int mouse = sys_open("/dev/mouse", 0, 0);

    if (fb == (uint32_t*)-1) {
        while (1) {
            char c;
            if (kbd >= 0 && sys_read(kbd, &c, 1) == 1) sys_write(1, &c, 1);
            else sys_sleep(16);
        }
    }

    while (1) {
        // Render
        draw_rect(0, 0, WIDTH, HEIGHT, COL_BG);
        draw_gradient(0, 0, WIDTH, 80, COL_BLACK, 0x073642u);
        draw_fox(20, 15, 3);
        draw_string(100, 30, "foxOS", COL_ORANGE);
        
        draw_rect_alpha(WIDTH - 600, 15, 580, 50, COL_BLACK, 100);
        draw_string(WIDTH - 580, 30, "SYSTEM", current_tab == TAB_SYSTEM ? COL_ACCENT : COL_FG);
        draw_string(WIDTH - 380, 30, "FILES", current_tab == TAB_FILES ? COL_ACCENT : COL_FG);
        draw_string(WIDTH - 180, 30, "CONSOLE", current_tab == TAB_CONSOLE ? COL_ACCENT : COL_FG);
        
        static int animated_tab_x = -1;
        int target_tab_x = WIDTH - 580 + current_tab * 200;
        if (animated_tab_x == -1) animated_tab_x = target_tab_x;
        if (animated_tab_x < target_tab_x) animated_tab_x += 20;
        if (animated_tab_x > target_tab_x) animated_tab_x -= 20;
        if (abs(animated_tab_x - target_tab_x) < 20) animated_tab_x = target_tab_x;
        draw_rect(animated_tab_x, 65, 100, 4, COL_ACCENT);
        
        switch (current_tab) {
            case TAB_SYSTEM: draw_system_tab(); break;
            case TAB_FILES: draw_files_tab(); break;
            case TAB_CONSOLE: draw_console_tab(); break;
        }

        draw_cursor(mouse_x, mouse_y);
        flip();

        // Input: Keyboard
        char c;
        if (sys_read(kbd, &c, 1) == 1) {
            if (c == '\t') current_tab = (current_tab + 1) % 3;
            else if (current_tab == TAB_CONSOLE) {
                if (c == '\n') {
                    if (cmd_len > 0) {
                        for(int i=0; i<128; i++) history[history_count % 16][i] = cmd_buf[i];
                        history_count++;
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

        // Input: Mouse
        mouse_event_t mev;
        while (sys_read(mouse, &mev, sizeof(mev)) == sizeof(mev)) {
            mouse_x += mev.dx;
            mouse_y -= mev.dy; // PS/2 Y is usually inverted
            if (mouse_x < 0) mouse_x = 0;
            if (mouse_y < 0) mouse_y = 0;
            if (mouse_x >= WIDTH) mouse_x = WIDTH - 1;
            if (mouse_y >= HEIGHT) mouse_y = HEIGHT - 1;
            mouse_buttons = mev.buttons;
            
            // Simple tab switching via mouse
            if (mouse_y < 80 && (mouse_buttons & 1)) {
                if (mouse_x > WIDTH - 600 && mouse_x < WIDTH - 400) current_tab = TAB_SYSTEM;
                else if (mouse_x > WIDTH - 400 && mouse_x < WIDTH - 200) current_tab = TAB_FILES;
                else if (mouse_x > WIDTH - 200) current_tab = TAB_CONSOLE;
            }
        }
        
        sys_getinfo(&info);
        if (info.uptime_ms - last_cursor_toggle > 500) {
            cursor_visible = !cursor_visible;
            last_cursor_toggle = info.uptime_ms;
        }
        sys_sleep(8); // High refresh rate for smooth mouse
    }
    return 0;
}

void _start(void) {
    __asm__ __volatile__(
        "xorq %%rbp, %%rbp\n"   // Clear RBP for stack traces
        "andq $-16, %%rsp\n"   // Align stack to 16 bytes
        "call main\n"          // Call C main
        "movq %%rax, %%rdi\n"  // Pass return code to exit
        "movq $1, %%rax\n"     // SYS_EXIT
        "syscall\n"            // Exit
        : : : "memory"
    );
    while(1);
}


#include <stdint.h>
#include "io.h"
#include "keyboard.h"
#include "serial.h"
#include "idt.h"
#include "../fs/devfs.h"
#include "../kernel/spinlock.h"

#define KBD_DATA 0x60
#define KBD_STATUS 0x64

// Multi-buffer keyboard support (broadcast)
static volatile int shell_buf[1024];
static volatile unsigned shell_head = 0, shell_tail = 0;

static volatile int user_buf[1024];
static volatile unsigned user_head = 0, user_tail = 0;

static spinlock_t kbd_lock = SPINLOCK_INIT;

static inline void kbd_wait_input_empty(void) { while (inb(KBD_STATUS) & 0x02) { } }
static inline int kbd_output_full(void) { return (inb(KBD_STATUS) & 0x01) != 0; }

// Modifier state
static uint8_t shift_down = 0;   // either LSHIFT (0x2A) or RSHIFT (0x36)
static uint8_t caps_lock = 0;    // toggled by 0x3A
static uint8_t e0_prefix = 0;    // track 0xE0 extended scancodes

static int shell_get(void) {
    __asm__ __volatile__("cli");
    spinlock_acquire(&kbd_lock);
    if (shell_head == shell_tail) {
        spinlock_release(&kbd_lock);
        __asm__ __volatile__("sti");
        return -1;
    }
    int ch = shell_buf[shell_tail];
    shell_tail = (shell_tail + 1) & (sizeof(shell_buf)-1);
    spinlock_release(&kbd_lock);
    __asm__ __volatile__("sti");
    serial_write("[kbd] shell get: "); serial_putc((char)ch); serial_writeln("");
    return ch;
}

static int user_get(void) {
    __asm__ __volatile__("cli");
    spinlock_acquire(&kbd_lock);
    if (user_head == user_tail) {
        spinlock_release(&kbd_lock);
        __asm__ __volatile__("sti");
        return -1;
    }
    int ch = user_buf[user_tail];
    user_tail = (user_tail + 1) & (sizeof(user_buf)-1);
    spinlock_release(&kbd_lock);
    __asm__ __volatile__("sti");
    serial_write("[kbd] user get: "); serial_putc((char)ch); serial_writeln("");
    return ch;
}

static int devfs_kbd_read(char* out, uint64_t max, uint64_t* outLen) {
    if (max == 0) return -1;
    int ch = user_get();
    if (ch == -1) {
        *outLen = 0;
        return 0;
    }
    out[0] = (char)ch;
    *outLen = 1;
    return 0;
}

static devfs_ops_t kbd_ops = {
    .read = devfs_kbd_read,
    .write = 0,
    .ioctl = 0,
    .mmap = 0
};

static int is_alpha(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }

// US scancode set 1 maps
static const char unshifted_map[128] = {
    [0x01]=0, [0x0E]='\b', [0x0F]='\t', [0x1C]='\n',
    [0x29]='`', [0x02]='1',[0x03]='2',[0x04]='3',[0x05]='4',[0x06]='5',[0x07]='6',[0x08]='7',[0x09]='8',[0x0A]='9',[0x0B]='0', [0x0C]='-', [0x0D]='=',
    [0x10]='q',[0x11]='w',[0x12]='e',[0x13]='r',[0x14]='t',[0x15]='y',[0x16]='u',[0x17]='i',[0x18]='o',[0x19]='p',[0x1A]='[',[0x1B]=']',
    [0x1E]='a',[0x1F]='s',[0x20]='d',[0x21]='f',[0x22]='g',[0x23]='h',[0x24]='j',[0x25]='k',[0x26]='l',[0x27]=';',[0x28]='\'',
    [0x2B]='\\',[0x2C]='z',[0x2D]='x',[0x2E]='c',[0x2F]='v',[0x30]='b',[0x31]='n',[0x32]='m',[0x33]=',',[0x34]='.',[0x35]='/',
    [0x39]=' '
};

static const char shifted_map[128] = {
    [0x01]=0, [0x0E]='\b', [0x0F]='\t', [0x1C]='\n',
    [0x29]='~', [0x02]='!',[0x03]='@',[0x04]='#',[0x05]='$',[0x06]='%',[0x07]='^',[0x08]='&',[0x09]='*',[0x0A]='(',[0x0B]=')', [0x0C]='_', [0x0D]='+',
    [0x10]='Q',[0x11]='W',[0x12]='E',[0x13]='R',[0x14]='T',[0x15]='Y',[0x16]='U',[0x17]='I',[0x18]='O',[0x19]='P',[0x1A]='{',[0x1B]='}',
    [0x1E]='A',[0x1F]='S',[0x20]='D',[0x21]='F',[0x22]='G',[0x23]='H',[0x24]='J',[0x25]='K',[0x26]='L',[0x27]=':',[0x28]='"',
    [0x2B]='|',[0x2C]='Z',[0x2D]='X',[0x2E]='C',[0x2F]='V',[0x30]='B',[0x31]='N',[0x32]='M',[0x33]='<',[0x34]='>',[0x35]='?',
    [0x39]=' '
};

static void keyboard_isr(registers_t* regs) {
    (void)regs;
    
    // Process all available scancodes
    while (kbd_output_full()) {
        uint8_t sc = inb(KBD_DATA);

        spinlock_acquire(&kbd_lock);

        if (sc == 0xE0) { e0_prefix = 1; spinlock_release(&kbd_lock); continue; }

        if (sc & 0x80) {
            uint8_t make = sc & 0x7F;
            if (e0_prefix) { e0_prefix = 0; spinlock_release(&kbd_lock); continue; }
            if (make == 0x2A || make == 0x36) shift_down = 0;
            spinlock_release(&kbd_lock);
            continue;
        }

        if (e0_prefix) {
            e0_prefix = 0;
            if (sc == 0x48) {
                // Put in shell buffer
                unsigned n_shell = (shell_head + 1) & (sizeof(shell_buf)-1);
                if (n_shell != shell_tail) { shell_buf[shell_head] = KBD_KEY_UP; shell_head = n_shell; }
                // Put in user buffer
                unsigned n_user = (user_head + 1) & (sizeof(user_buf)-1);
                if (n_user != user_tail) { user_buf[user_head] = KBD_KEY_UP; user_head = n_user; }
            }
            if (sc == 0x50) {
                // Put in shell buffer
                unsigned n_shell = (shell_head + 1) & (sizeof(shell_buf)-1);
                if (n_shell != shell_tail) { shell_buf[shell_head] = KBD_KEY_DOWN; shell_head = n_shell; }
                // Put in user buffer
                unsigned n_user = (user_head + 1) & (sizeof(user_buf)-1);
                if (n_user != user_tail) { user_buf[user_head] = KBD_KEY_DOWN; user_head = n_user; }
            }
            spinlock_release(&kbd_lock);
            continue;
        }

        if (sc == 0x2A || sc == 0x36) { shift_down = 1; spinlock_release(&kbd_lock); continue; }
        if (sc == 0x3A) { caps_lock ^= 1; spinlock_release(&kbd_lock); continue; }

        char ch = 0;
        char base = unshifted_map[sc];
        if (base != 0) {
            if (is_alpha(base)) {
                int upper = (shift_down ^ caps_lock);
                ch = upper ? (char)(base - 'a' + 'A') : (char)(base | 0);
            } else {
                ch = shift_down ? shifted_map[sc] : base;
            }
        }

        if (ch) {
            // Put in shell buffer
            unsigned n_shell = (shell_head + 1) & (sizeof(shell_buf)-1);
            if (n_shell != shell_tail) {
                shell_buf[shell_head] = ch;
                shell_head = n_shell;
            }

            // Put in user buffer
            unsigned n_user = (user_head + 1) & (sizeof(user_buf)-1);
            if (n_user != user_tail) {
                user_buf[user_head] = ch;
                user_head = n_user;
            }
        }
        spinlock_release(&kbd_lock);
    }
}

void keyboard_init(void) {
    shell_head = shell_tail = 0;
    user_head = user_tail = 0;
    shift_down = 0; caps_lock = 0; e0_prefix = 0;

    // Flush the controller's buffer
    while (kbd_output_full()) {
        (void)inb(KBD_DATA);
    }

    idt_register_handler(0x21, keyboard_isr); // IRQ 1 is mapped to 0x21
    idt_unmask_irq(1); // Unmask IRQ 1 in PIC
    devfs_register("kbd", &kbd_ops);
}

int keyboard_getchar(void) {
    // Non-blocking: returns immediately with -1 if no key
    return shell_get();
}

int keyboard_getchar_blocking(void) {
    // Blocking: waits until a key is available
    while (1) {
        int ch = shell_get();
        if (ch != -1) return ch;
        // Yield to other threads with HLT
        __asm__ __volatile__("hlt");
    }
}

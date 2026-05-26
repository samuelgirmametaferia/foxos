#include <stdint.h>
#include "io.h"
#include "keyboard.h"
#include "idt.h"

#define KBD_DATA 0x60
#define KBD_STATUS 0x64

static volatile char buf[128];
static volatile unsigned head = 0, tail = 0;

static inline void kbd_wait_input_empty(void) { while (inb(KBD_STATUS) & 0x02) { } }
static inline int kbd_output_full(void) { return (inb(KBD_STATUS) & 0x01) != 0; }

// Modifier state
static uint8_t shift_down = 0;   // either LSHIFT (0x2A) or RSHIFT (0x36)
static uint8_t caps_lock = 0;    // toggled by 0x3A
static uint8_t e0_prefix = 0;    // track 0xE0 extended scancodes

static void buf_put(int c) {
    unsigned n = (head + 1) & (sizeof(buf)/sizeof(buf[0])-1);
    if (n != tail) { head = n; buf[head] = c; }
}

static int buf_get(void) {
    if (head == tail) return -1;
    tail = (tail + 1) & (sizeof(buf)-1);
    return buf[tail];
}

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
    
    // Always read to acknowledge the controller
    uint8_t sc = inb(KBD_DATA);

    if (sc == 0xE0) { e0_prefix = 1; return; }

    if (sc & 0x80) {
        uint8_t make = sc & 0x7F;
        if (e0_prefix) { e0_prefix = 0; return; }
        if (make == 0x2A || make == 0x36) shift_down = 0;
        return;
    }

    if (e0_prefix) {
        e0_prefix = 0;
        if (sc == 0x48) buf_put(KBD_KEY_UP);
        if (sc == 0x50) buf_put(KBD_KEY_DOWN);
        return;
    }

    if (sc == 0x2A || sc == 0x36) { shift_down = 1; return; }
    if (sc == 0x3A) { caps_lock ^= 1; return; }

    char ch;
    char base = unshifted_map[sc];
    if (base == 0) return;

    if (is_alpha(base)) {
        int upper = (shift_down ^ caps_lock);
        ch = upper ? (char)(base - 'a' + 'A') : (char)(base | 0);
    } else {
        ch = shift_down ? shifted_map[sc] : base;
    }

    if (ch) buf_put(ch);
}

void keyboard_init(void) {
    head = tail = 0;
    shift_down = 0; caps_lock = 0; e0_prefix = 0;
    idt_register_handler(0x21, keyboard_isr); // IRQ 1 is mapped to 0x21
}

int keyboard_getchar(void) {
    // Non-blocking: returns immediately with -1 if no key
    return buf_get();
}

int keyboard_getchar_blocking(void) {
    // Blocking: waits until a key is available
    // For now, this busywaits since we don't have full I/O blocking yet
    // In a real system, this would block on an interrupt handler
    while (1) {
        int ch = buf_get();
        if (ch != -1) return ch;
        // Yield to other threads with HLT
        __asm__ __volatile__("hlt");
    }
}

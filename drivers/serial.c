#include "serial.h"
#include "io.h"
#include "quiesce.h"
#include "timer.h"

static volatile int g_serial_quiesced = 0;

static void serial_quiesce_cb(int enter) {
    g_serial_quiesced = enter ? 1 : 0;
}

static int interrupts_enabled(void) {
    unsigned long flags;
    __asm__ __volatile__ (
        "pushfq\n\t"
        "pop %0\n\t"
        : "=r" (flags)
        :
        : "memory"
    );
    return (flags & (1ul << 9)) != 0;
}

static void serial_wait_quiesce(void) {
    if (!g_serial_quiesced) return;
    if (!interrupts_enabled()) return;
    while (g_serial_quiesced) timer_sleep(1);
}

#define COM1_BASE 0x3F8

static int serial_is_transmit_empty(void) {
    return inb(COM1_BASE + 5) & 0x20;
}

static void serial_write_locked(const char* s) {
    uint64_t flags;
    __asm__ __volatile__ (
        "pushfq\n\t"
        "pop %0\n\t"
        "cli\n\t"
        : "=r" (flags)
        :
        : "memory"
    );

    while (*s) {
        serial_putc(*s++);
    }

    __asm__ __volatile__ (
        "push %0\n\t"
        "popfq\n\t"
        :
        : "r" (flags)
        : "memory", "cc"
    );
}

void serial_write(const char* s) {
    serial_wait_quiesce();
    serial_write_locked(s);
}


void serial_writeln(const char* s) {
    serial_wait_quiesce();
    serial_write_locked(s);
    serial_putc('\n');
}

void serial_init(void) {
    outb(COM1_BASE + 1, 0x00);
    outb(COM1_BASE + 3, 0x80);
    outb(COM1_BASE + 0, 0x03);
    outb(COM1_BASE + 1, 0x00);
    outb(COM1_BASE + 3, 0x03);
    outb(COM1_BASE + 2, 0xC7);
    outb(COM1_BASE + 4, 0x0B);
    quiesce_register(serial_quiesce_cb);
}

void serial_putc(char c) {
    while (!serial_is_transmit_empty()) {
        __asm__ __volatile__("pause");
    }
    outb(COM1_BASE, (uint8_t)c);
}

void serial_u64(uint64_t v) {
    if (v == 0) {
        serial_putc('0');
        return;
    }
    char tmp[32];
    int i = 0;
    while (v > 0) {
        tmp[i++] = (char)('0' + (v % 10));
        v /= 10;
    }
    while (i > 0) {
        serial_putc(tmp[--i]);
    }
}

void serial_u32(uint32_t v) {
    serial_u64((uint64_t)v);
}

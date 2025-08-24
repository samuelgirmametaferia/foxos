#include "serial.h"
#include "io.h"

#define COM1_BASE 0x3F8

static inline void com1_wait_tx_empty(void){
    while ((inb(COM1_BASE + 5) & 0x20) == 0) { }
}

void serial_init(void){
    // Disable interrupts
    outb(COM1_BASE + 1, 0x00);
    // Enable DLAB
    outb(COM1_BASE + 3, 0x80);
    // Set baud to 115200 (divisor 1)
    outb(COM1_BASE + 0, 0x01); // DLL
    outb(COM1_BASE + 1, 0x00); // DLM
    // 8 bits, no parity, one stop, clear DLAB
    outb(COM1_BASE + 3, 0x03);
    // Enable FIFO, clear them, 14-byte threshold
    outb(COM1_BASE + 2, 0xC7);
    // Modem control: RTS/DSR set
    outb(COM1_BASE + 4, 0x0B);
}

void serial_putc(char c){
    if (c == '\n') {
        com1_wait_tx_empty(); outb(COM1_BASE, '\r');
    }
    com1_wait_tx_empty();
    outb(COM1_BASE, (uint8_t)c);
}

void serial_write(const char* s){
    while (*s) serial_putc(*s++);
}

void serial_writeln(const char* s){
    serial_write(s);
    serial_putc('\n');
}

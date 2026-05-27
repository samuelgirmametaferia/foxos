#include "mouse.h"
#include "io.h"
#include "serial.h"
#include "../kernel/idt.h"
#include "../fs/devfs.h"
#include "../kernel/spinlock.h"

#define MOUSE_PORT   0x60
#define MOUSE_STATUS 0x64
#define MOUSE_ABIT   0x02
#define MOUSE_BBIT   0x01
#define MOUSE_WRITE  0xD4
#define MOUSE_F_BIT  0x20
#define MOUSE_V_BIT  0x08

static uint8_t mouse_cycle = 0;
static int8_t  mouse_byte[3];

typedef struct {
    int dx;
    int dy;
    uint32_t buttons;
} mouse_event_t;

#define MOUSE_BUF_SIZE 256
static mouse_event_t mouse_buf[MOUSE_BUF_SIZE];
static volatile unsigned mouse_head = 0;
static volatile unsigned mouse_tail = 0;
static spinlock_t mouse_lock = SPINLOCK_INIT;

static inline void mouse_wait(uint8_t a_type) {
    uint32_t _time_out = 10000;
    if (a_type == 0) {
        while (_time_out--) {
            if ((inb(MOUSE_STATUS) & 1) == 1) return;
        }
    } else {
        while (_time_out--) {
            if ((inb(MOUSE_STATUS) & 2) == 0) return;
        }
    }
}

static inline void mouse_write(uint8_t a_write) {
    mouse_wait(1);
    outb(MOUSE_STATUS, MOUSE_WRITE);
    mouse_wait(1);
    outb(MOUSE_PORT, a_write);
}

static uint8_t mouse_read(void) {
    mouse_wait(0);
    return inb(MOUSE_PORT);
}

static void mouse_isr(registers_t* regs) {
    (void)regs;
    uint8_t status = inb(MOUSE_STATUS);
    while (status & MOUSE_BBIT) {
        if (status & MOUSE_F_BIT) {
            int8_t mouse_in = inb(MOUSE_PORT);
            switch (mouse_cycle) {
                case 0:
                    mouse_byte[0] = mouse_in;
                    if (!(mouse_in & MOUSE_V_BIT)) return;
                    mouse_cycle++;
                    break;
                case 1:
                    mouse_byte[1] = mouse_in;
                    mouse_cycle++;
                    break;
                case 2:
                    mouse_byte[2] = mouse_in;
                    mouse_cycle = 0;
                    
                    if (mouse_byte[0] & 0x80 || mouse_byte[0] & 0x40) break;
                    
                    mouse_event_t ev;
                    ev.buttons = mouse_byte[0] & 0x07;
                    ev.dx = mouse_byte[1];
                    ev.dy = mouse_byte[2];
                    
                    unsigned n = (mouse_head + 1) & (MOUSE_BUF_SIZE - 1);
                    if (n != mouse_tail) {
                        mouse_buf[mouse_head] = ev;
                        mouse_head = n;
                    }
                    break;
            }
        } else {
            break;
        }
        status = inb(MOUSE_STATUS);
    }
}

static int devfs_mouse_read(char* out, uint64_t max, uint64_t* outLen) {
    if (max < sizeof(mouse_event_t)) return -1;
    
    spinlock_acquire(&mouse_lock);
    if (mouse_head == mouse_tail) {
        spinlock_release(&mouse_lock);
        *outLen = 0;
        return 0;
    }
    
    mouse_event_t ev = mouse_buf[mouse_tail];
    mouse_tail = (mouse_tail + 1) & (MOUSE_BUF_SIZE - 1);
    spinlock_release(&mouse_lock);
    
    *(mouse_event_t*)out = ev;
    *outLen = sizeof(mouse_event_t);
    return 0;
}

static devfs_ops_t mouse_ops = {
    .read = devfs_mouse_read,
    .write = 0,
    .ioctl = 0,
    .mmap = 0
};

void mouse_init(void) {
    uint8_t _status;
    mouse_wait(1);
    outb(MOUSE_STATUS, 0xA8);
    
    mouse_wait(1);
    outb(MOUSE_STATUS, 0x20);
    mouse_wait(0);
    _status = (inb(MOUSE_PORT) | 2);
    mouse_wait(1);
    outb(MOUSE_STATUS, 0x60);
    mouse_wait(1);
    outb(MOUSE_PORT, _status);
    
    mouse_write(0xF6);
    mouse_read();
    
    mouse_write(0xF4);
    mouse_read();
    
    idt_register_handler(0x2C, mouse_isr); // IRQ 12
    devfs_register("mouse", &mouse_ops);
}

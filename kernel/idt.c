#include "idt.h"
#include "io.h"
#include "serial.h"
#include "sched.h"

typedef struct {
    uint16_t offset_low;
    uint16_t sel;
    uint8_t ist;
    uint8_t flags;
    uint16_t offset_mid;
    uint32_t offset_high;
    uint32_t zero;
} __attribute__((packed)) idt_entry_t;

typedef struct {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed)) idt_ptr_t;

static idt_entry_t idt[256] __attribute__((aligned(16)));
static idt_ptr_t idt_p;
static isr_t interrupt_handlers[256];
static const uint16_t KERNEL_CODE_SELECTOR = 0x38;

extern void idt_load(void*);
extern uint64_t isr_stub_table[];

static void idt_set_gate(uint8_t num, uint64_t base, uint16_t sel, uint8_t flags) {
    idt[num].offset_low = (uint16_t)(base & 0xFFFFu);
    idt[num].offset_mid = (uint16_t)((base >> 16) & 0xFFFFu);
    idt[num].offset_high = (uint32_t)((base >> 32) & 0xFFFFFFFFu);
    idt[num].sel = sel;
    idt[num].ist = 0;
    idt[num].flags = flags;
    idt[num].zero = 0;
}

static void pic_remap(void) {
    outb(0x20, 0x11); io_wait();
    outb(0xA0, 0x11); io_wait();
    outb(0x21, 0x20); io_wait();
    outb(0xA1, 0x28); io_wait();
    outb(0x21, 0x04); io_wait();
    outb(0xA1, 0x02); io_wait();
    outb(0x21, 0x01); io_wait();
    outb(0xA1, 0x01); io_wait();
}

void idt_init(void) {
    __asm__ __volatile__("cli");
    for (int i = 0; i < 256; i++) {
        idt_set_gate((uint8_t)i, isr_stub_table[i], KERNEL_CODE_SELECTOR, 0x8E);
        interrupt_handlers[i] = 0;
    }
    pic_remap();
    outb(0x21, 0xFF);
    outb(0xA1, 0xFF);
    idt_p.limit = (uint16_t)(sizeof(idt_entry_t) * 256 - 1);
    idt_p.base = (uint64_t)(uintptr_t)&idt;
    idt_load(&idt_p);
}

void idt_enable_interrupts(void) {
    outb(0x21, 0xFC);
    outb(0xA1, 0xFF);
    __asm__ __volatile__("sti");
}

void idt_register_handler(uint8_t n, isr_t handler) {
    interrupt_handlers[n] = handler;
}

static const char* exception_messages[] = {
    "Division By Zero", "Debug", "Non Maskable Interrupt", "Breakpoint",
    "Into Detected Overflow", "Out of Bounds", "Invalid Opcode", "No Coprocessor",
    "Double Fault", "Coprocessor Segment Overrun", "Bad TSS", "Segment Not Present",
    "Stack Fault", "General Protection Fault", "Page Fault", "Unknown Interrupt",
    "Coprocessor Fault", "Alignment Check", "Machine Check", "Reserved",
    "Reserved", "Reserved", "Reserved", "Reserved", "Reserved", "Reserved",
    "Reserved", "Reserved", "Reserved", "Reserved", "Security Exception", "Reserved"
};

registers_t* interrupt_handler(registers_t* regs) {
    registers_t* out = regs;
    if (regs->int_no >= 32 && regs->int_no < 48) {
        if (regs->int_no >= 40) outb(0xA0, 0x20);
        outb(0x20, 0x20);
    }

    if (interrupt_handlers[regs->int_no] != 0) {
        interrupt_handlers[regs->int_no](regs);
    } else if (regs->int_no < 32) {
        serial_write("\n[PANIC] Exception: ");
        serial_write(exception_messages[regs->int_no]);
        serial_write(" (Int ");
        char buf[24];
        uint64_t v = regs->int_no;
        int n = 0;
        if (v == 0) {
            buf[n++] = '0';
            buf[n] = 0;
        } else {
            char t[24];
            int ti = 0;
            while (v) {
                t[ti++] = (char)('0' + (v % 10));
                v /= 10;
            }
            while (ti--) buf[n++] = t[ti];
            buf[n] = 0;
        }
        serial_write(buf);
        serial_writeln(")");
        for (;;) ;
    }

    if (regs->int_no == 32) {
        out = scheduler_tick(regs);
    }

    return out;
}

#include "idt.h"
#include "io.h"
#include "serial.h"
#include "sched.h"
#include "console.h"
#include "../common/lib.h"

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
static isr_t reserved_handlers[256];
static const uint16_t KERNEL_CODE_SELECTOR = 0x08;

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

void idt_mask_irq(uint8_t irq) {
    uint16_t port = (irq < 8) ? 0x21 : 0xA1;
    uint8_t line = (uint8_t)(irq & 7u);
    uint8_t mask = inb(port);
    mask |= (uint8_t)(1u << line);
    outb(port, mask);
}

void idt_unmask_irq(uint8_t irq) {
    uint16_t port = (irq < 8) ? 0x21 : 0xA1;
    uint8_t line = (uint8_t)(irq & 7u);
    uint8_t mask = inb(port);
    mask &= (uint8_t)~(1u << line);
    outb(port, mask);
}


int idt_is_exception(uint8_t int_no) {
    return int_no < 32;
}

int idt_is_irq(uint8_t int_no) {
    return int_no >= 32 && int_no < 48;
}

const char* idt_get_exception_name(uint8_t int_no) {
    static const char* exception_messages[] = {
        "Division By Zero", "Debug", "Non Maskable Interrupt", "Breakpoint",
        "Into Detected Overflow", "Out of Bounds", "Invalid Opcode", "No Coprocessor",
        "Double Fault", "Coprocessor Segment Overrun", "Bad TSS", "Segment Not Present",
        "Stack Fault", "General Protection Fault", "Page Fault", "Unknown Interrupt",
        "Coprocessor Fault", "Alignment Check", "Machine Check", "SIMD Exception",
        "Virtualization Exception", "Control Protection Exception", "Reserved",
        "Reserved", "Reserved", "Reserved", "Reserved", "Reserved", "Reserved",
        "Hypervisor Injection Exception", "VMM Communication Exception", "Security Exception"
    };
    if (int_no < 32) return exception_messages[int_no];
    return "Unknown Exception";
}

void idt_panic_handler(registers_t* regs, const char* message) {
    extern void rust_exception_handler(registers_t* regs, const char* message);
    rust_exception_handler(regs, message);
    for(;;);
}

static void double_fault_handler(registers_t* regs) {
    idt_panic_handler(regs, "DOUBLE FAULT");
}

static void gpf_handler(registers_t* regs) {
    idt_panic_handler(regs, "GENERAL PROTECTION FAULT");
}

static void page_fault_handler(registers_t* regs) {
    uint64_t cr2 = 0;
    __asm__ __volatile__("mov %%cr2, %0" : "=r"(cr2));
    extern void serial_write_panic(const char* s);
    serial_write_panic("\n[PANIC] Page Fault @ ");
    char ad_buf[32]; u64_to_hex(cr2, ad_buf);
    serial_write_panic(ad_buf);
    serial_write_panic("\n");
    idt_panic_handler(regs, "PAGE FAULT");
}

void idt_init(void) {
    __asm__ __volatile__("cli");
    for (int i = 0; i < 256; i++) {
        idt_set_gate((uint8_t)i, isr_stub_table[i], KERNEL_CODE_SELECTOR, 0x8E);
        interrupt_handlers[i] = 0;
        reserved_handlers[i] = 0;
    }
    pic_remap();
    outb(0x21, 0xFF);
    outb(0xA1, 0xFF);
    idt_p.limit = (uint16_t)(sizeof(idt_entry_t) * 256 - 1);
    idt_p.base = (uint64_t)(uintptr_t)&idt;
    idt_load(&idt_p);

    idt_register_handler(8, double_fault_handler);
    idt_register_handler(13, gpf_handler);
    idt_register_handler(14, page_fault_handler);
    serial_writeln("[idt] IDT initialized successfully");
}

void idt_enable_interrupts(void) {
    // Rely on IOAPIC/APIC for interrupt routing. 
    // Legacy PIC unmasking removed to prevent double interrupts.
    __asm__ __volatile__("sti");
    serial_writeln("[idt] CPU interrupts enabled");
}

void idt_load_for_ap(void) {
    idt_load(&idt_p);
}

void idt_register_handler(uint8_t n, isr_t handler) {
    interrupt_handlers[n] = handler;
}

void idt_register_reserved_handler(uint8_t n, isr_t handler) {
    reserved_handlers[n] = handler;
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

void pic_send_eoi(uint8_t irq) {
    if (irq >= 8) outb(0xA0, 0x20);
    outb(0x20, 0x20);
}

registers_t* interrupt_handler(registers_t* regs) {
    registers_t* out = regs;

    /* Send EOI (End of Interrupt) for PIC IRQs (32-47) */
    if (regs->int_no >= 32 && regs->int_no < 48) {
        // Send EOI to legacy PIC
        if (regs->int_no >= 40) outb(0xA0, 0x20);
        outb(0x20, 0x20);

        // Send EOI to Local APIC if present
        uint32_t* apic_eoi = (uint32_t*)0xFEE000B0;
        *apic_eoi = 0;
    }

    /* Check for registered handler first */
    if (interrupt_handlers[regs->int_no] != 0) {
        interrupt_handlers[regs->int_no](regs);
    }
    /* Check for reserved handler */
    else if (regs->int_no < 32 && reserved_handlers[regs->int_no] != 0) {
        reserved_handlers[regs->int_no](regs);
    }
    /* Handle unhandled exceptions */
    else if (regs->int_no < 32) {
        extern void rust_exception_handler(registers_t* regs, const char* message);
        rust_exception_handler(regs, exception_messages[regs->int_no]);

        serial_write("\n[PANIC] Exception: ");
        serial_write(exception_messages[regs->int_no]);
        serial_write(" (Int ");
        serial_u64(regs->int_no);
        serial_writeln(")");

        console_set_color(15, 4); // White on Red
        console_write("\n!!! KERNEL PANIC: ");
        console_write(exception_messages[regs->int_no]);
        console_write(" !!!\n");

        if (regs->int_no == 13) {  /* General Protection Fault */
            serial_write("[PANIC] GPF rip="); serial_u64(regs->rip);
            serial_write(" cs="); serial_u64(regs->cs);
            serial_write(" rflags="); serial_u64(regs->rflags);
            serial_write(" err="); serial_u64(regs->err_code);
            serial_writeln("");

            console_write("GPF at RIP: 0x");
            char rip_buf[32]; u64_to_hex(regs->rip, rip_buf);
            console_writeln(rip_buf);
        }

        if (regs->int_no == 14) { /* Page Fault (if not handled by registered handler) */
             uint64_t cr2 = 0;
             __asm__ __volatile__("mov %%cr2, %0" : "=r"(cr2));
             console_write("PF at ADDR: 0x");
             char ad_buf[32]; u64_to_hex(cr2, ad_buf);
             console_writeln(ad_buf);
        }

        for (;;) ;
    }

    /* Call scheduler tick for timer interrupt (IRQ 0 = int 32) */
    if (regs->int_no == 32) {
        out = scheduler_tick(regs);
    }
    
    /* Call scheduler tick for APIC timer interrupt (Vector 34) */
    if (regs->int_no == 34) {
        out = scheduler_tick(regs);
    }

    return out;
}

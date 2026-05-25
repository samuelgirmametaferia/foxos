#pragma once
#include <stdint.h>

// Minimal frame for long-mode ring 0 interrupts.
typedef struct {
    uint64_t rax, rbx, rcx, rdx, rsi, rdi, rbp;
    uint64_t r8, r9, r10, r11, r12, r13, r14, r15;
    uint64_t int_no, err_code;
    uint64_t rip, cs, rflags;
} __attribute__((packed)) registers_t;

typedef void (*isr_t)(registers_t*);

void idt_init(void);
void idt_enable_interrupts(void);
void idt_register_handler(uint8_t n, isr_t handler);
void idt_mask_irq(uint8_t irq);
void idt_unmask_irq(uint8_t irq);

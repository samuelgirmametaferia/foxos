BITS 64
default rel

section .text

extern interrupt_handler

%macro isr_noerr 1
global isr%1
isr%1:
    push qword 0
    push qword %1
    jmp isr_common
%endmacro

%macro isr_err 1
global isr%1
isr%1:
    push qword %1
    jmp isr_common
%endmacro

%assign i 0
%rep 256
    %if i == 8 || i == 10 || i == 11 || i == 12 || i == 13 || i == 14 || i == 17 || i == 30
        isr_err i
    %else
        isr_noerr i
    %endif
%assign i i+1
%endrep

isr_common:
    cld
    push r15
    push r14
    push r13
    push r12
    push r11
    push r10
    push r9
    push r8
    push rbp
    push rdi
    push rsi
    push rdx
    push rcx
    push rbx
    push rax

    mov rdi, rsp
    call interrupt_handler
    mov rsp, rax

    pop rax
    pop rbx
    pop rcx
    pop rdx
    pop rsi
    pop rdi
    pop rbp
    pop r8
    pop r9
    pop r10
    pop r11
    pop r12
    pop r13
    pop r14
    pop r15
    add rsp, 16
    iretq

global idt_load
idt_load:
    lidt [rdi]
    ret

global isr_stub_table
isr_stub_table:
%assign i 0
%rep 256
    dq isr %+ i
%assign i i+1
%endrep

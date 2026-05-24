BITS 64
default rel

global kernel_entry
extern kernel_main
extern __bss_start
extern __bss_end

section .bss
align 16
kernel_stack:
    resb 32768
kernel_stack_top:

section .text
kernel_entry:
    mov rbx, rdi

    lea rdi, [__bss_start]
    lea rcx, [__bss_end]
    sub rcx, rdi
    xor eax, eax
    rep stosb

    lea rsp, [kernel_stack_top]
    and rsp, -16

    mov rdi, rbx
    call kernel_main
.hang:
    hlt
    jmp .hang

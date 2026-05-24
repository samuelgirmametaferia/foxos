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
    ; save boot_info pointer
    mov rbx, rdi

    ; clear BSS
    lea rdi, [__bss_start]
    lea rcx, [__bss_end]
    sub rcx, rdi
    xor eax, eax
    rep stosb

    ; setup stack
    lea rsp, [kernel_stack_top]
    and rsp, -16

    ; Build identity page tables to cover up to 16GiB (map 0..16GiB using 2MiB pages)
    ; Layout in BSS: pml4, pdpt, pd0..pd15
    ; PML4[0] -> PDPT
    lea rax, [pml4]
    lea rdx, [pdpt]
    mov rcx, rdx
    or rcx, 0x3
    mov [rax], rcx

    ; Fill PDPT entries (point to pd0..pd15)
    lea rax, [pdpt]
    lea r8, [pd0]
    lea r9, [pd1]
    lea r10, [pd2]
    lea r11, [pd3]
    lea r14, [pd4]
    lea r15, [pd5]
    ; store first 6
    mov rcx, r8
    or rcx, 0x3
    mov [rax], rcx
    mov rcx, r9
    or rcx, 0x3
    mov [rax+8], rcx
    mov rcx, r10
    or rcx, 0x3
    mov [rax+16], rcx
    mov rcx, r11
    or rcx, 0x3
    mov [rax+24], rcx
    mov rcx, r14
    or rcx, 0x3
    mov [rax+32], rcx
    mov rcx, r15
    or rcx, 0x3
    mov [rax+40], rcx

    ; remaining pd pointers (pd6..pd15)
    lea r12, [pd6]
    lea r13, [pd7]
    lea rdi, [pd8]
    lea rsi, [pd9]
    lea rdx, [pd10]
    lea rbx, [pd11]
    lea r11, [pd12]
    lea r10, [pd13]
    lea r9, [pd14]
    lea r8, [pd15]
    mov rcx, r12
    or rcx, 0x3
    mov [rax+48], rcx
    mov rcx, r13
    or rcx, 0x3
    mov [rax+56], rcx
    mov rcx, rdi
    or rcx, 0x3
    mov [rax+64], rcx
    mov rcx, rsi
    or rcx, 0x3
    mov [rax+72], rcx
    mov rcx, rdx
    or rcx, 0x3
    mov [rax+80], rcx
    mov rcx, rbx
    or rcx, 0x3
    mov [rax+88], rcx
    mov rcx, r11
    or rcx, 0x3
    mov [rax+96], rcx
    mov rcx, r10
    or rcx, 0x3
    mov [rax+104], rcx
    mov rcx, r9
    or rcx, 0x3
    mov [rax+112], rcx
    mov rcx, r8
    or rcx, 0x3
    mov [rax+120], rcx

    ; Fill PD entries with 2MiB pages
    xor r12, r12        ; pd index 0..15
    xor r13, r13        ; global 2MiB page index
.fill_pd_tables:
    cmp r12, 16
    jge .done_pd_fill

    ; select PD base in rdi
    cmp r12, 0
    je .use_pd0
    cmp r12, 1
    je .use_pd1
    cmp r12, 2
    je .use_pd2
    cmp r12, 3
    je .use_pd3
    cmp r12, 4
    je .use_pd4
    cmp r12, 5
    je .use_pd5
    cmp r12, 6
    je .use_pd6
    cmp r12, 7
    je .use_pd7
    cmp r12, 8
    je .use_pd8
    cmp r12, 9
    je .use_pd9
    cmp r12, 10
    je .use_pd10
    cmp r12, 11
    je .use_pd11
    cmp r12, 12
    je .use_pd12
    cmp r12, 13
    je .use_pd13
    cmp r12, 14
    je .use_pd14
    jmp .use_pd15
.use_pd0: lea rdi, [pd0]; jmp .pd_base_chosen
.use_pd1: lea rdi, [pd1]; jmp .pd_base_chosen
.use_pd2: lea rdi, [pd2]; jmp .pd_base_chosen
.use_pd3: lea rdi, [pd3]; jmp .pd_base_chosen
.use_pd4: lea rdi, [pd4]; jmp .pd_base_chosen
.use_pd5: lea rdi, [pd5]; jmp .pd_base_chosen
.use_pd6: lea rdi, [pd6]; jmp .pd_base_chosen
.use_pd7: lea rdi, [pd7]; jmp .pd_base_chosen
.use_pd8: lea rdi, [pd8]; jmp .pd_base_chosen
.use_pd9: lea rdi, [pd9]; jmp .pd_base_chosen
.use_pd10: lea rdi, [pd10]; jmp .pd_base_chosen
.use_pd11: lea rdi, [pd11]; jmp .pd_base_chosen
.use_pd12: lea rdi, [pd12]; jmp .pd_base_chosen
.use_pd13: lea rdi, [pd13]; jmp .pd_base_chosen
.use_pd14: lea rdi, [pd14]; jmp .pd_base_chosen
.use_pd15: lea rdi, [pd15]
.pd_base_chosen:
    xor rsi, rsi        ; entry counter 0..511
.fill_pd_entries:
    cmp rsi, 512
    jge .next_pd
    mov rax, r13
    shl rax, 21         ; phys = r13 * 2MiB
    or rax, 0x83        ; present | rw | page-size
    mov rcx, rsi
    shl rcx, 3
    mov [rdi + rcx], rax
    inc rsi
    inc r13
    jmp .fill_pd_entries
.next_pd:
    inc r12
    jmp .fill_pd_tables
.done_pd_fill:

    ; set CR4.PAE
    mov rax, cr4
    or rax, (1<<5)
    mov cr4, rax

    ; load PML4 physical address into CR3
    lea rax, [pml4]
    mov cr3, rax

    ; enable paging (CR0.PG)
    mov rax, cr0
    or rax, 0x80000000
    mov cr0, rax

    ; restore boot_info pointer and call C entry
    mov rdi, rbx
    call kernel_main
.hang:
    hlt
    jmp .hang

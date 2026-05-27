; BITS 16 Real-Mode to BITS 64 Long-Mode Trampoline for AP Booting
; Loaded at physical address 0x8000
org 0x8000

BITS 16
section .text
global _start
_start:
    jmp real_start              ; Short jump to real startup code (2 bytes)

    ; Pad to 8 bytes to align the shared variables
    align 8
pml4_ptr:     dq 0              ; Offset 8:  PML4 base physical address (64-bit)
ap_stack_ptr: dq 0              ; Offset 16: Stack pointer allocated for the AP (64-bit)
ap_c_entry:   dq 0              ; Offset 24: Address of the 64-bit C entry point (64-bit)
ap_cpu_id:    dq 0              ; Offset 32: Assigned logical CPU ID (64-bit)
ap_apic_id:   dq 0              ; Offset 40: Assigned hardware APIC ID (64-bit)

real_start:
    cli                         ; Disable interrupts on this AP
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax

    ; Load temporary GDT
    lgdt [gdt_descriptor]

    ; Enable protected mode in CR0
    mov eax, cr0
    or eax, 1
    mov cr0, eax

    ; Far jump to 32-bit segment
    jmp 0x08:prot_mode_32

BITS 32
prot_mode_32:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov ss, ax

    ; Enable PAE (Physical Address Extension) in CR4
    mov eax, cr4
    or eax, (1 << 5)
    mov cr4, eax

    ; Load PML4 physical address into CR3
    mov eax, [pml4_ptr]
    mov cr3, eax

    ; Enable Long Mode (LME) in EFER MSR (0xC0000080)
    mov ecx, 0xC0000080
    rdmsr
    or eax, (1 << 8)
    wrmsr

    ; Enable Paging (PG) in CR0
    mov eax, cr0
    or eax, 0x80000000
    mov cr0, eax

    ; Far jump to 64-bit Long Mode
    jmp 0x18:long_mode_64

BITS 64
long_mode_64:
    ; Set up 64-bit segment registers
    mov ax, 0x20
    mov ds, ax
    mov es, ax
    mov ss, ax

    ; Spin-wait for the BSP to assign a stack for this specific core
.wait_stack:
    mov rbx, [ap_stack_ptr]
    test rbx, rbx
    jz .wait_stack

    ; Set up stack pointer
    mov rsp, rbx

    ; Clear the stack pointer variable to signal the BSP that we are booted and have claimed the stack
    xor rax, rax
    mov [ap_stack_ptr], rax

    ; Jump to the 64-bit C entry point
    mov rax, [ap_c_entry]
    call rax

.hang:
    hlt
    jmp .hang

align 8
gdt_start:
    ; Null descriptor
    dq 0x0000000000000000
    ; 32-bit Code Segment: type=0x9A, granularity=0xCF
    dq 0x00cf9a000000ffff
    ; 32-bit Data Segment: type=0x92, granularity=0xCF
    dq 0x00cf92000000ffff
    ; 64-bit Code Segment: type=0x98 (exec/read), long_mode=1
    dq 0x0020980000000000
    ; 64-bit Data Segment: type=0x92 (read/write)
    dq 0x0000920000000000
gdt_end:

gdt_descriptor:
    dw gdt_end - gdt_start - 1
    dd gdt_start

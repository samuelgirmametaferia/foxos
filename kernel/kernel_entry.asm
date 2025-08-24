; 32-bit kernel entry
[bits 32]

global kernel_entry
global pm_to_rm_reboot
extern kernel_main
extern __bss_start
extern __bss_end

kernel_entry:
    ; Optional: ensure a known stack (bootloader already set one)
    ; mov esp, 0x90000

    ; Zero .bss
    mov edi, __bss_start    ; dest
    mov ecx, __bss_end      ; ecx = end
    sub ecx, edi            ; ecx = size in bytes
    xor eax, eax            ; fill = 0
    shr ecx, 2              ; dwords
    rep stosd
    ; handle remaining bytes
    mov ecx, __bss_end
    sub ecx, edi            ; recalc remaining after dwords -> bytes
    and ecx, 3
    rep stosb

    call kernel_main
.hang:
    hlt
    jmp .hang

; Switch from protected mode to real mode and far-jump to 0x7000:0x0000
; Assumes interrupts are disabled by caller.
pm_to_rm_reboot:
    ; Mask PIC and disable interrupts in PM already done by caller; also mask PIC here to be safe
    mov al, 0xFF
    out 0x21, al
    out 0xA1, al
    ; Load a real-mode style IVT (base=0, limit=0x3FF) so INT works after switching to RM
    lidt [pm_realmode_idt]
    ; Clear PE bit in CR0 to enter real mode
    mov eax, cr0
    and eax, 0xFFFFFFFE
    mov cr0, eax
    ; Far jump to the 16-bit stub copied at 0x7000:0x0000
    o16 jmp 0x7000:0x0000
    hlt
    jmp $

pm_null_idt:
    dw 0
    dd 0

pm_realmode_idt:
    dw 0x03FF      ; limit
    dd 0x00000000  ; base

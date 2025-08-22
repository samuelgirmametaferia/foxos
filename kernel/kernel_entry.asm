; 32-bit kernel entry
[bits 32]

global kernel_entry
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

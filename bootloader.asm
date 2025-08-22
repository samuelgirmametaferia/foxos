[BITS 16]
[ORG 0x7C00]

%ifndef KERNEL_SECTORS
%define KERNEL_SECTORS 32
%endif

%define KERNEL_LOAD_SEG  0x1000       ; 0x1000:0000 -> 0x00010000
%define KERNEL_LOAD_OFF  0x0000
%define KERNEL_LOAD_ADDR 0x00010000

%define SPT 18                        ; sectors per track (1.44MB floppy)
%define HEADS 2                       ; heads

%define FB_BOOTINFO_SEG 0x7000        ; physical 0x00070000
%define VBE_MODE 0x118                ; 1024x768x16bpp (common)

; Define ENABLE_VBE to enable graphics mode set and bootinfo handoff
; %define ENABLE_VBE 1

start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00

    ; Ensure VGA text mode 80x25
    mov ax, 0x0003
    int 0x10

    ; Preserve boot drive
    mov [boot_drive], dl

    ; Debug: print 'S' at start
    mov al, 'S'
    call print_char

    ; Set destination pointer ES:BX = 0x1000:0000
    mov ax, KERNEL_LOAD_SEG
    mov es, ax
    xor bx, bx

    ; Initialize CHS to C=0, H=0, S=2 (skip boot sector)
    xor dx, dx             ; DH=head=0, DL ignored here
    xor cx, cx             ; CH=cyl=0
    mov si, KERNEL_SECTORS ; SI = remaining sectors
    mov byte [sector], 2
    mov byte [head], 0
    mov byte [cyl], 0

load_loop:
    cmp si, 0
    je load_done

    ; Prepare CHS
    mov dl, [boot_drive]   ; drive
    mov ah, 0x02           ; read sectors
    mov al, 1              ; read 1 sector per call (simple, small)
    mov ch, [cyl]          ; cylinder
    mov dh, [head]         ; head
    mov cl, [sector]       ; sector (1..SPT)

    ; BIOS read: INT 13h, retry on error
    push si                ; preserve remaining count
    mov bp, 3              ; retry count
.read_retry:
    int 0x13
    jnc .read_ok
    dec bp
    jz disk_error
    mov ah, 0x00           ; reset disk
    mov dl, [boot_drive]
    int 0x13
    jmp .read_retry
.read_ok:
    pop si

    ; Advance destination ES:BX by 512 bytes
    add bx, 512
    jnc .no_carry
    mov ax, es
    add ax, 0x20            ; 512 bytes = 32 paragraphs
    mov es, ax
.no_carry:

    ; Decrement remaining
    dec si

    ; Advance CHS: S++ ; if S>SPT: S=1,H++; if H==HEADS: H=0,C++
    inc byte [sector]
    mov al, [sector]
    cmp al, SPT+1
    jb load_loop
    mov byte [sector], 1
    inc byte [head]
    mov al, [head]
    cmp al, HEADS
    jb load_loop
    mov byte [head], 0
    inc byte [cyl]
    jmp load_loop

load_done:
%ifdef ENABLE_VBE
    ; Try to set a VBE LFB mode and write boot framebuffer info at 0x70000
    push ds
    push es
    mov ax, 0x4F02
    mov bx, VBE_MODE | 0x4000  ; request LFB
    int 0x10
    cmp ax, 0x004F
    jne .vbe_done

    ; Query mode info into 0x7000:0100
    mov ax, FB_BOOTINFO_SEG
    mov es, ax
    mov di, 0x0100
    mov ax, 0x4F01
    mov cx, VBE_MODE
    int 0x10
    cmp ax, 0x004F
    jne .vbe_done

    ; Write bootinfo header at 0x7000:0000
    ; magic 0xB007F00D
    mov ax, FB_BOOTINFO_SEG
    mov es, ax
    mov dword [es:0], 0xB007F00D
    mov word  [es:4], 1       ; present
    mov word  [es:6], 0       ; reserved
    ; width, height, pitch, bpp from mode info at es:0100
    mov ax, [es:0x0100 + 0x12]
    mov [es:8], ax
    mov ax, [es:0x0100 + 0x14]
    mov [es:10], ax
    mov ax, [es:0x0100 + 0x10]
    mov [es:12], ax
    mov al, [es:0x0100 + 0x19]
    mov [es:14], al
    mov byte [es:15], 0
    mov eax, [es:0x0100 + 0x28]
    mov [es:16], eax

.vbe_done:
    pop es
    pop ds
%endif

    ; Debug: print 'L' after load
    mov al, 'L'
    call print_char

    ; Enable A20 via fast gate
    in al, 0x92
    or al, 00000010b
    out 0x92, al

    ; Load GDT and enter protected mode
    lgdt [gdt_descriptor]
    mov eax, cr0
    or eax, 1
    mov cr0, eax
    jmp 0x08:protected_mode

; Tiny teletype print (AL=char)
print_char:
    push ax
    push bx
    mov ah, 0x0E
    mov bh, 0x00
    mov bl, 0x07
    int 0x10
    pop bx
    pop ax
    ret

[BITS 32]
protected_mode:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov esp, 0x90000

    mov eax, KERNEL_LOAD_ADDR
    jmp eax

[BITS 16]
disk_error:
    cli
.hang:
    hlt
    jmp .hang

; ---- GDT (flat) ----
gdt_start:
    dq 0x0000000000000000
    dq 0x00CF9A0000000000 | 0x000000000000FFFF   ; code
    dq 0x00CF920000000000 | 0x000000000000FFFF   ; data
gdt_end:

gdt_descriptor:
    dw gdt_end - gdt_start - 1
    dd gdt_start

; ---- Data ----
boot_drive: db 0
sector: db 0
head:   db 0
cyl:    db 0

; Boot signature
TIMES 510 - ($ - $$) db 0
DW 0xAA55

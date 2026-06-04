BITS 64
default rel

global syscall_entry
extern syscall_handler

; Syscall entry point
; On entry:
; RCX = User RIP
; R11 = User RFLAGS
; CS = Kernel CS, SS = Kernel SS
; RSP = User RSP

syscall_entry:
    swapgs

    ; Save user RSP temporarily in a percpu scratch space.
    ; Let's use GS:[0x18] for scratch space.
    mov gs:[0x18], rsp

    ; Load kernel stack from GS:[0x10]
    mov rsp, gs:[0x10]

    ; Now we are on the kernel stack. Push registers.
    push qword gs:[0x18] ; User RSP
    push r11             ; User RFLAGS
    push rcx             ; User RIP

    push rbx
    push rbp
    push r12
    push r13
    push r14
    push r15
    push rdi
    push rsi
    push rdx
    push r8
    push r9
    push r10

    ; The syscall calling convention (System V AMD64 ABI for syscalls):
    ; RAX = System call number
    ; RDI, RSI, RDX, R10, R8, R9 = Args 1-6
    ; System V C ABI expects: RDI, RSI, RDX, RCX, R8, R9
    ; Move R10 (syscall arg4) to RCX (C arg4)
    mov rcx, r10

    ; We pass sys_num in arg7 (stack)
    push rax ; 7th argument on stack

    ; Call the handler
    call syscall_handler

    ; Remove 7th arg
    add rsp, 8

    ; Restore registers
    pop r10
    pop r9
    pop r8
    pop rdx
    pop rsi
    pop rdi
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbp
    pop rbx

    ; Restore user RIP, RFLAGS, RSP
    pop rcx
    pop r11
    pop rsp

    swapgs
    
    ; Return to user mode
    o64 sysret

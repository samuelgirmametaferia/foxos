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
; We need to switch to kernel stack.
; To do this, we use the `swapgs` instruction to get the kernel's per-cpu data in GS.
; We will assume the kernel stack top is stored at GS:[0] (or similar).
; Actually, our percpu struct has the kernel stack pointer or we can use the TSS rsp0.
; Wait, we have the TSS rsp0 in our percpu data? Our percpu init doesn't set GS:[0] to kernel stack.
; Let's just define a percpu variable for the kernel stack top for syscalls.
; But wait, sysenter/syscall doesn't load RSP from TSS! Only interrupts do.
; So we must read the kernel stack from GS manually.
; Let's assume GS:[0x08] holds the kernel stack top for this CPU.

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

    ; The syscall calling convention (System V AMD64 ABI for syscalls):
    ; RAX = System call number
    ; RDI, RSI, RDX, R10, R8, R9 = Args 1-6
    ; (Note: normal function uses RCX for arg4, syscall uses R10 because RCX is used for RIP)

    ; We pass sys_num in arg7 (stack) or we just pass it as arg7.
    ; C signature: uint64_t syscall_handler(rdi, rsi, rdx, r10, r8, r9, sys_num)
    ; sys_num is currently in rax.
    push rax ; 7th argument on stack

    ; Call the handler
    call syscall_handler

    ; Remove 7th arg
    add rsp, 8

    ; Restore callee-saved registers
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
    ; SYSRETQ expects:
    ; RCX = RIP
    ; R11 = RFLAGS
    ; It will load CS and SS from STAR MSR + 16/8
    o64 sysret

bits 64

section .text
global syscall_handler
extern syscall_dispatch

;
; syscall_handler — Linux x86_64 SYSCALL entry point
;
; On SYSCALL entry from user mode the CPU has:
;   RAX = syscall number
;   RDI = arg1, RSI = arg2, RDX = arg3, R10 = arg4, R8 = arg5, R9 = arg6
;   RCX = saved user RIP  (SYSCALL saves it here)
;   R11 = saved user RFLAGS
;   RSP = still user RSP (NOT switched by CPU — we must switch manually)
;   CS/SS loaded from STAR MSR for kernel mode
;   IF cleared by SFMASK
;
; Linux ABI note: arg4 is passed in R10 (not RCX, which the CPU clobbers).
;
; We build a struct syscall_frame on the kernel stack that matches the
; layout declared in syscall.h.
;

syscall_handler:
    ; Swap GS to get access to kernel per-CPU area
    swapgs

    ; Save user RSP into per-cpu.user_scratch (offset 16)
    mov     [gs:16], rsp

    ; Load kernel RSP from per-cpu.kernel_stack (offset 8)
    mov     rsp, [gs:8]

    ; Push user RSP onto kernel stack (saves it context-switch safely)
    push    qword [gs:16]

    ; Save caller-saved registers and syscall ABI registers onto kernel stack.
    ; We save all GP regs to form the syscall_frame struct (matches syscall.h).
    push    r15
    push    r14
    push    r13
    push    r12
    push    rbp
    push    rbx
    ; r11 = user RFLAGS (saved by SYSCALL), r10 = arg4
    push    r11
    push    r10
    push    r9
    push    r8
    push    rdx
    push    rsi
    push    rdi
    push    rcx
    ; rax = syscall number
    push    rax

    ; Enable interrupts now that we have a stable stack
    sti

    ; Call C dispatcher: syscall_dispatch(frame)
    ; frame pointer is RSP right now
    mov     rdi, rsp
    mov     r15, rsp            ; save RSP across call
    and     rsp, -16            ; align stack for ABI
    call    syscall_dispatch
    mov     rsp, r15            ; restore RSP

    ; Return value is in RAX — store it in the frame's rax slot
    ; (syscall_dispatch returns int64_t which becomes the user RAX)
    mov     [rsp], rax          ; frame->rax = return value

    ; Disable interrupts before SYSRETQ
    cli

    ; Restore registers
    pop     rax     ; user return value (syscall result)
    pop     rcx     ; user RIP (restored into RCX for SYSRETQ)
    pop     rdi
    pop     rsi
    pop     rdx
    pop     r8
    pop     r9
    pop     r10
    pop     r11     ; user RFLAGS (restored by SYSRETQ from R11)
    pop     rbx
    pop     rbp
    pop     r12
    pop     r13
    pop     r14
    pop     r15

    ; Restore user RSP from the stack
    pop     rsp

    ; Swap GS back to userspace GS
    swapgs

    ; SYSRETQ: restores RIP←RCX, RFLAGS←R11, switches CS/SS to user segments
    o64 sysret

global syscall_return_to_user
syscall_return_to_user:
    ; RDI points to the syscall_frame struct
    mov     rsp, rdi
    cli
    pop     rax     ; return value (0 for child)
    pop     rcx     ; user RIP
    pop     rdi
    pop     rsi
    pop     rdx
    pop     r8
    pop     r9
    pop     r10
    pop     r11     ; user RFLAGS
    pop     rbx
    pop     rbp
    pop     r12
    pop     r13
    pop     r14
    pop     r15
    pop     rsp     ; user RSP
    swapgs
    o64 sysret

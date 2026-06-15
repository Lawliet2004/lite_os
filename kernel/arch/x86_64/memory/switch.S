bits 64
section .text

global context_switch
context_switch:
    push rbx
    push rbp
    push r12
    push r13
    push r14
    push r15

    mov [rdi], rsp
    mov rsp, rsi

    pop r15
    pop r14
    pop r13
    pop r12
    pop rbp
    pop rbx
    ret

global jump_to_usermode
jump_to_usermode:
    ; rdi = entry_point
    ; rsi = user_stack_top
    cli

    ; Load user data segments (RPL=3). With the Phase 3b GDT layout
    ; the user-data selector is at GDT[3] (selector 0x18 | RPL 3 = 0x1b)
    ; and the user-code selector is at GDT[4] (0x20 | 3 = 0x23). The
    ; old layout had them at 0x28/0x30 which is now where the BSP's
    ; TSS lives, so jumping there faults with a TSS GPF.
    mov ax, 0x1b
    mov ds, ax
    mov es, ax
    mov fs, ax
    swapgs
    mov gs, ax

    ; Build iretq frame
    push qword 0x1b     ; SS
    push rsi            ; RSP
    push qword 0x202    ; RFLAGS
    push qword 0x23     ; CS
    push rdi            ; RIP

    iretq

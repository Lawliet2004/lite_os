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

    ; Load user data segments (RPL=3)
    mov ax, 0x2b
    mov ds, ax
    mov es, ax
    mov fs, ax
    swapgs
    mov gs, ax

    ; Build iretq frame
    push qword 0x2b     ; SS
    push rsi            ; RSP
    push qword 0x202    ; RFLAGS
    push qword 0x33     ; CS
    push rdi            ; RIP

    iretq

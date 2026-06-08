section .text

global ltenix_setjmp
ltenix_setjmp:
    ; rdi = jmp_buf_t env
    mov [rdi + 0], rbx
    mov [rdi + 8], rsp
    mov [rdi + 16], rbp
    mov [rdi + 24], r12
    mov [rdi + 32], r13
    mov [rdi + 40], r14
    mov [rdi + 48], r15
    mov rax, [rsp]         ; return address
    mov [rdi + 56], rax
    xor eax, eax           ; return 0
    ret

global ltenix_longjmp
ltenix_longjmp:
    ; rdi = jmp_buf_t env
    ; rsi = int val
    mov rbx, [rdi + 0]
    mov r8,  [rdi + 8]     ; saved rsp
    mov rbp, [rdi + 16]
    mov r12, [rdi + 24]
    mov r13, [rdi + 32]
    mov r14, [rdi + 40]
    mov r15, [rdi + 48]
    mov rax, [rdi + 56]    ; return address

    ; set up return value: if val (rsi) is 0, we must return 1, else val.
    mov rdx, 1
    test esi, esi
    cmovz esi, edx         ; if val == 0, val = 1

    ; restore rsp and place return address at top of stack
    mov rsp, r8
    mov [rsp], rax

    mov eax, esi           ; return val
    ret

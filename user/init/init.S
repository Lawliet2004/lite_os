bits 64
section .text
global _start

_start:
    ; Print message: write(1, message, message_len)
    mov rax, 1                  ; SYS_write
    mov rdi, 1                  ; fd 1 (stdout)
    lea rsi, [rel message]      ; buffer
    mov rdx, message_len        ; length
    syscall

    ; Exit: exit(0)
    mov rax, 60                 ; SYS_exit
    mov rdi, 0                  ; status 0
    syscall

section .data
message:
    db "Hello from userspace init program!", 10
message_len equ $ - message

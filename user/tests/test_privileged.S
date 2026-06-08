bits 64
section .text
global _start

_start:
    ; Attempt to execute a privileged CPU instruction.
    ; This must trigger a General Protection Fault (Vector 13).
    hlt

    ; Exit if we survive
    mov rax, 60
    mov rdi, 2
    syscall

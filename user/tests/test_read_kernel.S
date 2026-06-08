bits 64
section .text
global _start

_start:
    ; Attempt to read from kernel virtual address space.
    ; This must trigger a Page Fault (Vector 14).
    mov rax, [0xffffffff80000000]

    ; If we somehow survive (which shouldn't happen), exit.
    mov rax, 60
    mov rdi, 1
    syscall

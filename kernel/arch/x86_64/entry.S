bits 64

section .text
global _start
extern kernel_main

_start:
    cli
    lea rsp, [rel stack_top]
    xor rbp, rbp
    call kernel_main

.halt:
    cli
    hlt
    jmp .halt

section .bss
align 16
stack_bottom:
    resb 16384
stack_top:

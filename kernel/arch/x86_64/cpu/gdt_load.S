bits 64

section .text
global gdt_load

; void gdt_load(const struct gdtr *gdtr, uint16_t code, uint16_t data, uint16_t tss)
gdt_load:
    lgdt [rdi]

    mov ax, dx
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax

    push rsi
    lea rax, [rel .reload_cs]
    push rax
    retfq

.reload_cs:
    mov ax, cx
    ltr ax
    ret

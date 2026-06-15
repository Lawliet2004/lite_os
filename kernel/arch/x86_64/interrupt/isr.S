bits 64

section .text
extern exception_dispatch
extern irq_dispatch

%macro ISR_NO_ERROR 1
global isr%1
isr%1:
    push qword 0
    push qword %1
    jmp isr_common
%endmacro

%macro ISR_ERROR 1
global isr%1
isr%1:
    push qword %1
    jmp isr_common
%endmacro

ISR_NO_ERROR 0
ISR_NO_ERROR 1
ISR_NO_ERROR 2
ISR_NO_ERROR 3
ISR_NO_ERROR 4
ISR_NO_ERROR 5
ISR_NO_ERROR 6
ISR_NO_ERROR 7
ISR_ERROR 8
ISR_NO_ERROR 9
ISR_ERROR 10
ISR_ERROR 11
ISR_ERROR 12
ISR_ERROR 13
ISR_ERROR 14
ISR_NO_ERROR 15
ISR_NO_ERROR 16
ISR_ERROR 17
ISR_NO_ERROR 18
ISR_NO_ERROR 19
ISR_NO_ERROR 20
ISR_ERROR 21
ISR_NO_ERROR 22
ISR_NO_ERROR 23
ISR_NO_ERROR 24
ISR_NO_ERROR 25
ISR_NO_ERROR 26
ISR_NO_ERROR 27
ISR_NO_ERROR 28
ISR_NO_ERROR 29
ISR_NO_ERROR 30
ISR_NO_ERROR 31

; Phase 3d: IPI stubs. The LAPIC delivers inter-processor interrupts
; by writing the IPI's vector to the ICR and pulsing the local APIC.
; The vector is the same as any IDT entry, so the LAPIC's vector
; field selects the IDT gate and we end up here. Each IPI has a
; dedicated stub so the C-side dispatcher can branch on the vector
; in a single load — no per-IPI handler-array lookup needed.
;
; The IPI handlers expect to run on a CPU that's already in long
; mode with the kernel page tables loaded, so we don't swapgs (the
; sender already did; if it didn't, the receiver is the BSP, which
; is fine because the BSP's GS_BASE is the kernel per-CPU data). */
%macro IPI_NO_ERROR 1
global isr_ipi%1
isr_ipi%1:
    push qword 0
    push qword (0x40 + %1)
    jmp isr_ipi_common
%endmacro

IPI_NO_ERROR 0      ; vector 0x40 - reschedule
IPI_NO_ERROR 1      ; vector 0x41 - TLB shootdown
IPI_NO_ERROR 2      ; vector 0x42 - panic

%macro IRQ 2
global irq%1
irq%1:
    push qword 0
    push qword %2
    jmp irq_common
%endmacro

IRQ 0, 32
IRQ 1, 33
IRQ 2, 34
IRQ 3, 35
IRQ 4, 36
IRQ 5, 37
IRQ 6, 38
IRQ 7, 39
IRQ 8, 40
IRQ 9, 41
IRQ 10, 42
IRQ 11, 43
IRQ 12, 44
IRQ 13, 45
IRQ 14, 46
IRQ 15, 47

isr_common:
    push r15
    push r14
    push r13
    push r12
    push r11
    push r10
    push r9
    push r8
    push rsi
    push rdi
    push rbp
    push rdx
    push rcx
    push rbx
    push rax

    ; Check if we came from userspace (low 2 bits of CS selector on stack)
    test qword [rsp + 144], 3
    jz .kernel_entry
    swapgs
.kernel_entry:

    mov rdi, rsp
    mov r15, rsp
    and rsp, -16
    call exception_dispatch
    mov rsp, r15

    ; Check if we are returning to userspace
    test qword [rsp + 144], 3
    jz .kernel_exit
    swapgs
.kernel_exit:

    pop rax
    pop rbx
    pop rcx
    pop rdx
    pop rbp
    pop rdi
    pop rsi
    pop r8
    pop r9
    pop r10
    pop r11
    pop r12
    pop r13
    pop r14
    pop r15

    add rsp, 16
    iretq

irq_common:
    push r15
    push r14
    push r13
    push r12
    push r11
    push r10
    push r9
    push r8
    push rsi
    push rdi
    push rbp
    push rdx
    push rcx
    push rbx
    push rax

    ; Check if we came from userspace
    test qword [rsp + 144], 3
    jz .kernel_entry
    swapgs
.kernel_entry:

    mov rdi, rsp
    mov r15, rsp
    and rsp, -16
    call irq_dispatch
    mov rsp, r15

    ; Check if we are returning to userspace
    test qword [rsp + 144], 3
    jz .kernel_exit
    swapgs
.kernel_exit:

    pop rax
    pop rbx
    pop rcx
    pop rdx
    pop rbp
    pop rdi
    pop rsi
    pop r8
    pop r9
    pop r10
    pop r11
    pop r12
    pop r13
    pop r14
    pop r15

    add rsp, 16
    iretq

; Phase 3d: IPI common path. See top of file for the design.
global isr_ipi_common
extern ipi_dispatch

isr_ipi_common:
    push r15
    push r14
    push r13
    push r12
    push r11
    push r10
    push r9
    push r8
    push rsi
    push rdi
    push rbp
    push rdx
    push rcx
    push rbx
    push rax

    mov rdi, rsp
    mov r15, rsp
    and rsp, -16
    call ipi_dispatch
    mov rsp, r15

    pop rax
    pop rbx
    pop rcx
    pop rdx
    pop rbp
    pop rdi
    pop rsi
    pop r8
    pop r9
    pop r10
    pop r11
    pop r12
    pop r13
    pop r14
    pop r15

    add rsp, 16
    iretq

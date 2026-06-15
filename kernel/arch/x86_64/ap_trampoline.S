; ap_trampoline.S - Application-processor startup code for LiteNix SMP
;
; This file is copied verbatim into a 4 KiB low-memory page by the BSP.
; The page layout (offsets within the 4 KiB page):
;
;     0x000 - 0x07F : 16-bit real mode entry and transition code
;     0x080 - 0x085 : 16-bit GDTR (6 bytes: uint16 limit + uint32 base)
;     0x086         : (padding)
;     0x088 - 0x0A7 : 16-bit GDT (4 entries, 32 bytes total)
;     0x0A8 - 0x1FF : (unused, reserved)
;     0x200 - 0x2FF : 32-bit protected mode code (long-mode setup)
;     0x300 - 0x3FF : 64-bit long mode code (kernel entry)
;     0x400 - 0x4FF : parameter block filled in by the BSP:
;       0x400 : kernel PML4 physical address (uint64)
;       0x408 : kernel GDT base (linear, uint64)
;       0x410 : kernel GDT limit (uint16)
;       0x418 : kernel IDT base (linear, uint64)
;       0x420 : kernel IDT limit (uint16)
;       0x428 : per-CPU data virtual address (linear, uint64)
;       0x430 : kernel stack top (linear, uint64)
;       0x438 : cpu_id (uint64)
;       0x440 : LAPIC ID (uint32)
;       0x448 : (unused, padding for alignment)
;     0x500 - 0xEFF : (unused, reserved for future expansion)
;     0xF00 - 0xF7F : diagnostic region. Each byte is a "I got here"
;                      marker. The BSP reads this region after the
;                      SIPI timeout to determine how far the AP
;                      got before stalling. See the table of marker
;                      offsets in the 16-bit code below.
;     0xF80 - 0xFFF : 32-bit/64-bit trampoline stack region. The
;                      stack pointer in the 32-bit code is 0xFC0; the
;                      64-bit code inherits this and runs a few
;                      pushes before switching to the per-CPU
;                      kernel stack.
;
; Sequence:
;   1. 16-bit code: set up segments, patch GDTR base, LGDT, enter
;      protected mode (PE=1 in CR0), far-jump to 32-bit code.
;   2. 32-bit code: set PAE in CR4, LME in EFER, load CR3 with the
;      kernel's PML4, set PG in CR0. Then far-return to 64-bit code
;      (retf in 32-bit mode loads EIP+CS from the stack; the new CS
;      has L=1 so the CPU enters 64-bit long mode).
;   3. 64-bit code: load the kernel's GDT (via lgdt with a stack-resident
;      GDTR), far-jump to load new CS, reload DS/SS, load IDT, load
;      TSS, set kernel stack, install MSR_GS_BASE for per-CPU data,
;      call smp_ap_entry() in C.

bits 16
a32                                ; 32-bit addressing throughout 16-bit code
                                    ; (default in 16-bit mode is 16-bit, which
                                    ; truncates 32-bit registers like edi and
                                    ; can't address the trampoline's high
                                    ; physical pages. The 16-bit mode here
                                    ; only affects operand size, not the
                                    ; memory model.)

section .ap16

global ap_trampoline_16_start
global ap_trampoline_16_end

ap_trampoline_16_start:
    cli
    cld

    ; Phase 8: QEMU's "wait for SIPI" leaves each AP in protected mode
    ; with flat segments (CS=0x0008, DS=0x0010, base=0 for both) and
    ; the vector*4KB loaded into EIP. When the SIPI delivers, the AP
    ; starts executing at offset 0 of the trampoline in protected
    ; mode, not real mode. The previous version of this code
    ; assumed `DS = CS = trampoline_segment` (real-mode addressing)
    ; and ended up writing diag markers to physical 0xF00 instead
    ; of trampoline_phys + 0xF00 — every write landed in the wrong
    ; place. The QEMU monitor confirmed this: every diag byte was 0
    ; even though the BSP's canary at 0xF80 was intact.
    ;
    ; The fix: use 32-bit absolute addresses via the `a32` directive
    ; so `[addr]` always means linear address `addr`, regardless of
    ; segment base. The trampoline's address is not hard-coded
    ; because it's chosen at runtime by the BSP; the BSP writes the
    ; address to physical 0x9F000 (a page-aligned location in the
    ; Limine USABLE region) and we read it from there. The QEMU
    ; "wait for SIPI" code doesn't use 0x9F000, so we can use it
    ; as a scratch mailbox.

    a32
    mov edi, [0x9F000]            ; edi = trampoline's physical address

    ; ---- Phase 7 diagnostics: mark 16-bit entry reached ----
    ; Each marker is a single byte with a unique value; the BSP
    ; reads the whole region after the SIPI timeout and decodes
    ; which milestones fired. The diag region lives at trampoline
    ; offset 0xF00-0xF7F.
    mov byte [edi + 0xF00], 0x01   ; AP_DIAG_16_ENTRY

    ; Write a "16-bit code reached" marker to a FIXED address
    ; (0x9F800) so the BSP can verify the SIPI actually delivered
    ; even if the mailbox read returned a bogus edi. With paging
    ; off, linear 0x9F800 = physical 0x9F800.
    mov byte [0x9F800], 0xA1       ; AP_DIAG_16_REACHED_FIXED

    ; Also write a single character to the COM1 serial port so we
    ; can see in the QEMU log whether the 16-bit code is even
    ; running.
    mov dx, 0x3F8
    mov al, '1'
    out dx, al

    ; Patch the GDTR's base field. The GDT is at file offset 0x88 in
    ; the trampoline, so its absolute address is edi + 0x88.
    mov eax, edi
    add ax, 0x88            ; ax = GDT base low 16 bits
    mov [edi + 0x82], ax    ; write low 16 bits of GDTR base
    shr eax, 16
    mov [edi + 0x84], al    ; write high 8 bits of GDTR base

    ; Load the 16-bit GDT.
    lgdt [edi + 0x80]

    ; Enter protected mode (the AP is already in protected mode, so
    ; this is a no-op on the PE bit; but we keep it for clarity).
    mov eax, cr0
    or eax, 1
    mov cr0, eax

    ; Far return into the 32-bit code. The 16-bit ljmp instruction can
    ; only encode a 16-bit IP displacement, which the linker cannot
    ; always fit when the trampoline is placed at a high address by the
    ; kernel's linker script. So we use retf: the stack already has
    ; trampoline_32's offset (0x200) and segment selector (0x08) pushed
    ; below. This avoids any relocation that needs to reach across
    ; .ap16 and .ap32 sections.
    push word 0x200         ; target IP (offset 0x200 within the page)
    push word 0x08          ; target CS (32-bit code segment)
    retf

; The GDTR lives at offset 0x80 in the trampoline page. We only patch
; the base at runtime; the limit is a fixed constant.
align 16
gdt16_ptr:
    dw gdt16_end - gdt16 - 1   ; limit
    dd 0                       ; base — patched at runtime by the 16-bit code
gdt16_ptr_end:

; 16-bit GDT (4 entries, 32 bytes). Index 0 is the null descriptor;
; 1 (selector 0x08) is a 32-bit code segment; 2 (selector 0x10) is a
; 32-bit data segment; 3 (selector 0x18) is a 64-bit code segment used
; for the retf that enters long mode.
align 8
gdt16:
    dq 0                              ; 0x00: null descriptor
    dw 0xFFFF, 0x0000                 ; 0x08: 32-bit code, 4 GiB, DPL=0
    dw 0x9A00, 0x00CF
    dw 0xFFFF, 0x0000                 ; 0x10: 32-bit data, 4 GiB, DPL=0
    dw 0x9200, 0x00CF
    dw 0xFFFF, 0x0000                 ; 0x18: 64-bit code, 4 GiB, DPL=0, L=1
    dw 0x9A00, 0x00AF
gdt16_end:

ap_trampoline_16_end:

bits 32

section .ap32

global trampoline_32_start
global trampoline_32_end
trampoline_32_start:
trampoline_32:
    ; Now in 32-bit protected mode. CS=0x08, DS=ES=SS=0x10 (all set up by
    ; the 16-bit code and the GDT load). EDI = trampoline phys.

    mov byte [edi + 0xF04], 0x05   ; AP_DIAG_32_ENTRY

    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    ; Phase 4: keep the stack inside the trampoline's 4 KiB page so
    ; the BSP only has to map one 4 KiB region in the kernel PML4
    ; (Phase 2 originally used 0x7c00, which lives in the null page;
    ; that page is intentionally unmapped so that a null-pointer
    ; deref in kernel mode is caught. We can't map it here without
    ; disabling the null-page guard, so instead we set the stack
    ; inside the trampoline itself, well below the parameters at
    ; 0x400+). The 32-bit code does only 2 pushes (for the retf
    ; into 64-bit mode); the 64-bit code does a few more before
    ; it switches to the per-CPU kernel stack.
    mov esp, 0xfc0

    ; Enable PAE in CR4. Required for long mode.
    mov eax, cr4
    or eax, (1 << 5)        ; CR4.PAE
    mov cr4, eax
    mov byte [edi + 0xF05], 0x06   ; AP_DIAG_32_PAE_SET

    ; Set the LME (long mode enable) bit in IA32_EFER. We also set NXE
    ; and SCE since the kernel uses NX and SYSCALL.
    mov ecx, 0xC0000080
    rdmsr
    or eax, (1 << 8)         ; EFER.LME
    or eax, (1 << 11)        ; EFER.NXE
    or eax, (1 << 0)         ; EFER.SCE
    wrmsr
    mov byte [edi + 0xF06], 0x07   ; AP_DIAG_32_EFER_SET

    ; Load CR3 with the kernel's PML4 (read from parameter area).
    mov eax, [edi + 0x400]
    mov cr3, eax
    mov byte [edi + 0xF07], 0x08   ; AP_DIAG_32_CR3_LOADED

    ; Enable paging. CR0.PG=1 with PAE and LME set means we are now in
    ; long mode. We are still using 32-bit operand size until the
    ; far return at the end of this section.
    mov eax, cr0
    or eax, (1 << 31)
    mov cr0, eax
    mov byte [edi + 0xF08], 0x09   ; AP_DIAG_32_PG_SET

    ; Far return to enter 64-bit long mode. retf in 32-bit mode reads
    ; EIP then CS from the stack and loads them; because the new CS
    ; (0x18) has L=1 in the 16-bit GDT, the CPU enters 64-bit mode.
    ; The trampoline's 64-bit code is at offset 0x300 in the page, so
    ; EIP = trampoline_phys + 0x300.
    lea eax, [edi + 0x300]   ; EIP (32-bit, fits since phys < 1 MiB)
    push eax                 ; EIP
    push dword 0x18          ; CS (64-bit code selector)
    mov byte [edi + 0xF09], 0x0A   ; AP_DIAG_32_RETF (about to retf)
    retf

trampoline_32_end:

bits 64

section .ap64

global ap_trampoline_64_start
global ap_trampoline_64_end

ap_trampoline_64_start:
    ; Now in 64-bit long mode. RDI = trampoline phys (low 32 bits, the
    ; high 32 bits of RDI are zero). CS = 0x18 (64-bit code selector
    ; from the 16-bit GDT). We are executing at offset 0x300 in the
    ; trampoline page, which is identity-mapped (low memory is mapped
    ; by the kernel's PML4 because Limine sets it up for the bootloader).

    mov byte [rdi + 0xF0A], 0x0B   ; AP_DIAG_64_ENTRY

    ; Load the kernel GDT. The GDTR structure is a 16-bit limit
    ; followed by a 64-bit base; we build it on the stack.
    sub rsp, 16
    mov word [rsp], ax            ; limit at offset 0
    mov qword [rsp + 2], rax      ; base at offset 2
    lgdt [rsp]
    add rsp, 16
    mov byte [rdi + 0xF0B], 0x0C   ; AP_DIAG_64_LGDT_DONE

    ; Far jump to load the new CS (kernel CS = 0x08). retfq reads the
    ; return RIP and CS from the stack.
    push 0x08                     ; CS
    lea rax, [rel .reload_cs]
    push rax
    retfq
.reload_cs:
    ; CS is now 0x08 (kernel 64-bit code). The retfq above is the
    ; trickiest step in the entire trampoline: if the GDT base we
    ; passed to lgdt was wrong, or the descriptor at index 1 was
    ; malformed, the CS load would #GP and we'd never write the
    ; next marker. Putting the marker immediately below the retfq
    ; lets the BSP distinguish "got into 64-bit code but failed
    ; the retfq" (marker 0x0B set, 0x0D not) from "retfq
    ; succeeded" (0x0D set, 0x0E not). */
    mov byte [rdi + 0xF0C], 0x0D   ; AP_DIAG_64_RETFQ_DONE

    ; Reload DS/ES/SS with the kernel's selectors.
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax

    ; Load the kernel IDT. Same layout as GDTR.
    sub rsp, 16
    mov word [rsp], ax            ; limit at offset 0
    mov qword [rsp + 2], rax      ; base at offset 2
    lidt [rsp]
    add rsp, 16
    mov byte [rdi + 0xF0D], 0x0E   ; AP_DIAG_64_LIDT_DONE

    ; Load the TSS. For Phase 2 we share the BSP's TSS; per-CPU TSS
    ; is added in Phase 3.
    mov ax, 0x18
    ltr ax
    mov byte [rdi + 0xF0E], 0x0F   ; AP_DIAG_64_LTR_DONE

    ; Switch to the per-CPU kernel stack allocated by the BSP.
    mov rsp, [rdi + 0x430]
    mov byte [rdi + 0xF0F], 0x10   ; AP_DIAG_64_KSTACK

    ; Install MSR_GS_BASE to point at this CPU's per-CPU struct.
    ; wrmsr takes EDX:EAX as the 64-bit value, ECX as the MSR number.
    mov rax, [rdi + 0x428]       ; per-CPU data virtual address
    mov rdx, rax
    shr rdx, 32
    mov ecx, 0xC0000101          ; MSR_GS_BASE
    wrmsr
    mov byte [rdi + 0xF10], 0x11   ; AP_DIAG_64_WRMSR_GS

    ; Set MSR_KERNEL_GS_BASE = 0 so swapgs from usermode restores 0.
    xor eax, eax
    xor edx, edx
    mov ecx, 0xC0000102          ; MSR_KERNEL_GS_BASE
    wrmsr
    mov byte [rdi + 0xF11], 0x12   ; AP_DIAG_64_WRMSR_KGS

    ; Call the C entry point. rdi = cpu_id (read from parameter area
    ; and loaded into the first argument register). The C side
    ; marks g_ap_started[cpu_id] and parks the AP.
    mov rdi, [rdi + 0x438]       ; cpu_id
    mov byte [rdi + 0xF12], 0x13   ; AP_DIAG_64_BEFORE_CALL
    extern smp_ap_entry
    call smp_ap_entry

    ; smp_ap_entry should never return; if it does, halt forever.
.hang:
    mov byte [rdi + 0xF13], 0xFF   ; AP_DIAG_64_RETURNED (shouldn't happen)
    hlt
    jmp .hang

ap_trampoline_64_end:

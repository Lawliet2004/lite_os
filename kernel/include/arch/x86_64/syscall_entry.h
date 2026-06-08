#ifndef LITENIX_ARCH_X86_64_SYSCALL_ENTRY_H
#define LITENIX_ARCH_X86_64_SYSCALL_ENTRY_H

#include <stdint.h>

/*
 * MSR addresses for SYSCALL/SYSRET configuration.
 *
 * IA32_EFER  - bit 0 (SCE) must be set to enable SYSCALL/SYSRET.
 * IA32_STAR  - [47:32] kernel CS (also sets SS = CS+8)
 *              [63:48] user CS-8 for SYSRET (SYSRET sets CS=STAR[63:48]+16, SS=STAR[63:48]+8)
 * IA32_LSTAR - 64-bit RIP of syscall handler (long mode target)
 * IA32_SFMASK- RFLAGS bits to clear on SYSCALL entry (clears IF, DF, TF)
 */
#define MSR_EFER   0xC0000080U
#define MSR_STAR   0xC0000081U
#define MSR_LSTAR  0xC0000082U
#define MSR_SFMASK 0xC0000084U

/* MSR addresses for GS bases */
#define MSR_GS_BASE        0xC0000101U
#define MSR_KERNEL_GS_BASE 0xC0000102U

#define EFER_SCE   (1U << 0)   /* System Call Enable */

/*
 * Structure for per-CPU storage, accessed via GS segment base.
 * Struct offsets:
 *   offset 0: kernel_stack
 *   offset 8: user_scratch
 */
struct cpu_context {
    uint64_t kernel_stack;  /* offset 0 */
    uint64_t user_scratch;   /* offset 8 */
} __attribute__((packed));

/*
 * syscall_init() - program MSRs for SYSCALL/SYSRET.
 * Must be called after gdt_init() so the GDT is stable.
 */
void syscall_init(void);

/* Helper to set the kernel stack pointer for the current CPU */
void syscall_set_kernel_rsp(uint64_t rsp);

#endif

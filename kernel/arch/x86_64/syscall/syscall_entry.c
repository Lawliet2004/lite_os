#include <arch/x86_64/syscall_entry.h>
#include <arch/x86_64/gdt.h>
#include <kernel/printk.h>
#include <stdbool.h>
#include <stdint.h>

/*
 * The syscall_handler entry point is defined in syscall_stub.S.
 * It saves registers, calls syscall_dispatch(), and issues SYSRETQ.
 */
extern void syscall_handler(void);

/* BSP per-CPU CPU context */
static struct cpu_context bsp_cpu_context;

bool cpu_smap_supported = false;

void syscall_set_kernel_rsp(uint64_t rsp)
{
    bsp_cpu_context.kernel_stack = rsp;
}

static void wrmsr(uint32_t msr, uint64_t value)
{
    uint32_t lo = (uint32_t)(value & 0xFFFFFFFFU);
    uint32_t hi = (uint32_t)(value >> 32);
    __asm__ volatile ("wrmsr" : : "c"(msr), "a"(lo), "d"(hi) : "memory");
}

static uint64_t rdmsr(uint32_t msr)
{
    uint32_t lo, hi;
    __asm__ volatile ("rdmsr" : "=a"(lo), "=d"(hi) : "c"(msr) : "memory");
    return ((uint64_t)hi << 32) | lo;
}

void syscall_init(void)
{
    /* Point KERNEL_GS_BASE to the per-CPU context structure */
    wrmsr(MSR_KERNEL_GS_BASE, (uint64_t)(uintptr_t)&bsp_cpu_context);
    wrmsr(MSR_GS_BASE, 0); /* User GS base starts as 0 */

    /* Enable SCE bit in EFER */
    uint64_t efer = rdmsr(MSR_EFER);
    efer |= EFER_SCE;
    wrmsr(MSR_EFER, efer);

    /*
     * STAR layout:
     *   [47:32] = kernel CS selector  → SYSCALL sets CS=STAR[47:32], SS=STAR[47:32]+8
     *   [63:48] = user CS-16 selector → SYSRET  sets CS=STAR[63:48]+16, SS=STAR[63:48]+8
     *
     * We need:
     *   SYSCALL CS = KERNEL_CODE_SELECTOR (0x08)
     *   SYSRET  CS = USER_CODE_SELECTOR  (0x30) → STAR[63:48] = 0x30 - 16 = 0x20
     *   SYSRET  SS = USER_DATA_SELECTOR  (0x28) → STAR[63:48]+8 = 0x28 ✓
     *
     * So STAR[63:48] = USER_DATA_SELECTOR (0x28), because:
     *   SYSRET SS = STAR[63:48]+8  = 0x28+8 = 0x30 → wrong
     *
     * Correct: STAR[63:48] = USER_CODE_SELECTOR - 16 = 0x30 - 0x10 = 0x20
     *   SYSRET SS = 0x20 + 8  = 0x28 = USER_DATA_SELECTOR ✓
     *   SYSRET CS = 0x20 + 16 = 0x30 = USER_CODE_SELECTOR ✓
     */
    uint64_t star =
        ((uint64_t)KERNEL_CODE_SELECTOR << 32) |
        ((uint64_t)(USER_CODE_SELECTOR - 16) << 48);
    wrmsr(MSR_STAR, star);

    /* LSTAR = 64-bit address of syscall_handler */
    wrmsr(MSR_LSTAR, (uint64_t)syscall_handler);

    /*
     * SFMASK: flags to clear on SYSCALL entry.
     * Clear IF (bit 9)  — disable interrupts at entry, handler re-enables
     * Clear DF (bit 10) — direction flag safety
     * Clear TF (bit 8)  — trap flag safety
     * Clear AC (bit 18) — alignment check
     */
    wrmsr(MSR_SFMASK, (1U << 9) | (1U << 10) | (1U << 8) | (1U << 18));

    /* Check CPUID for SMEP/SMAP support and enable them in CR4 */
    uint32_t eax, ebx, ecx, edx;
    __asm__ volatile("cpuid"
                     : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
                     : "a"(7), "c"(0));

    /* Enable SSE in CR0 and CR4 */
    uint64_t cr0;
    __asm__ volatile("mov %%cr0, %0" : "=r"(cr0));
    cr0 &= ~(1ULL << 2);  /* Clear EM (coprocessor emulation) */
    cr0 |= (1ULL << 1);   /* Set MP (monitor coprocessor) */
    __asm__ volatile("mov %0, %%cr0" :: "r"(cr0) : "memory");

    uint64_t cr4;
    __asm__ volatile("mov %%cr4, %0" : "=r"(cr4));
    cr4 |= (1ULL << 9);   /* Set OSFXSR */
    cr4 |= (1ULL << 10);  /* Set OSXMMEXCPT */

    if (ebx & (1U << 7)) {
        cr4 |= (1ULL << 20); /* Enable SMEP */
        printk("CPU: SMEP supported and enabled\n");
    } else {
        printk("CPU: SMEP not supported\n");
    }

    if (ebx & (1U << 20)) {
        cr4 |= (1ULL << 21); /* Enable SMAP */
        cpu_smap_supported = true;
        printk("CPU: SMAP supported and enabled\n");
    } else {
        printk("CPU: SMAP not supported\n");
    }

    __asm__ volatile("mov %0, %%cr4" : : "r"(cr4) : "memory");
}

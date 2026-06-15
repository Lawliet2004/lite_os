/*
 * gdt.c - Global Descriptor Table (Phase 3b: per-CPU TSS).
 *
 * Each logical CPU needs its own TSS so that:
 *   - The kernel stack pointer (rsp0) used for ring 3 -> ring 0
 *     transitions is per-CPU.
 *   - The IST 1 stack used by the #DF handler is per-CPU; otherwise
 *     a double fault on CPU 1 would clobber CPU 0's stack.
 *
 * We allocate one TSS descriptor per CPU in the GDT, starting at
 * index 3 (selector 0x18, the original TSS_SELECTOR). CPU N's TSS
 * descriptor lives at gdt[3 + 2*N .. 4 + 2*N] and is selected by
 * selector 0x18 + N*16. The shared user segments (DPL=3) follow
 * after all the TSS entries.
 *
 * Phase 2 still does not actually run the APs, so this code is
 * exercised only by the BSP today; it is the data structure the
 * APs will use when Phase 2's identity-map issue is resolved.
 */

#include <arch/x86_64/gdt.h>
#include <arch/x86_64/smp.h>
#include <kernel/compiler.h>
#include <lib/string.h>
#include <stdint.h>

#define TSS_AVAILABLE_64 0x89
#define MAX_TSS_IN_GDT   MAX_CPUS

struct gdtr {
    uint16_t limit;
    uint64_t base;
} PACKED;

struct tss64 {
    uint32_t reserved0;
    uint64_t rsp0;
    uint64_t rsp1;
    uint64_t rsp2;
    uint64_t reserved1;
    uint64_t ist1;
    uint64_t ist2;
    uint64_t ist3;
    uint64_t ist4;
    uint64_t ist5;
    uint64_t ist6;
    uint64_t ist7;
    uint32_t reserved2;
    uint16_t reserved3;
    uint16_t iomap_base;
} PACKED;

/* GDT layout (Phase 3b):
 *
 *   index  entry
 *   -----  -----
 *   0      null descriptor
 *   1      kernel code (DPL=0, long mode)  selector 0x08
 *   2      kernel data (DPL=0)             selector 0x10
 *   3      user data (DPL=3)               selector 0x18
 *   4      user code (DPL=3, long mode)     selector 0x20
 *   5..6   CPU 0 TSS  (16 bytes)           selector 0x28
 *   7..8   CPU 1 TSS  (16 bytes)           selector 0x38
 *   ...
 *   5+2N   CPU N TSS  (16 bytes)           selector 0x28 + N*16
 *
 * The shared user segments stay at their old selectors (0x18 / 0x20)
 * so the existing syscall path and tss_set_rsp0 callers don't need
 * to be updated. The TSS descriptors start at index 5 and each
 * occupies two 8-byte GDT slots (the 64-bit TSS descriptor format).
 *
 * Total entries for MAX_CPUS=64: 5 + 2*64 = 133. */
#define MAX_TSS_IN_GDT   MAX_CPUS
#define GDT_USER_DATA_IDX 3
#define GDT_USER_CODE_IDX 4
#define GDT_FIRST_TSS_IDX 5

uint64_t gdt[5 + 2 * MAX_TSS_IN_GDT];

/* Per-CPU TSS. One TSS struct per logical CPU, with that CPU's
 * kernel stack and IST 1 (double-fault) stack. */
struct tss64 tss_array[MAX_CPUS];

/* Per-CPU double-fault stacks. 16 KiB each; 16 KiB * 64 = 1 MiB. */
static uint8_t tss_df_stack[MAX_CPUS][16384] __attribute__((aligned(16)));

extern void gdt_load(const struct gdtr *gdtr, uint16_t code_selector,
                     uint16_t data_selector, uint16_t tss_selector);

uint64_t read_rsp(void)
{
    uint64_t value;
    __asm__ volatile ("mov %%rsp, %0" : "=r"(value));
    return value;
}

uint64_t read_rbp(void)
{
    uint64_t value;
    __asm__ volatile ("mov %%rbp, %0" : "=r"(value));
    return value;
}

/* TSS descriptor for this CPU: gdt[5 + 2*cpu_id .. 6 + 2*cpu_id].
 * Selector is (5 + 2*cpu_id) * 8 = 0x28 + cpu_id * 16. */
static void set_tss_descriptor(int cpu_id, uint64_t base, uint32_t limit)
{
    int base_idx = GDT_FIRST_TSS_IDX + 2 * cpu_id;
    gdt[base_idx] =
        ((uint64_t)(limit & 0xFFFF)) |
        ((base & 0xFFFFFFULL) << 16) |
        ((uint64_t)TSS_AVAILABLE_64 << 40) |
        (((uint64_t)(limit >> 16) & 0xFULL) << 48) |
        (((base >> 24) & 0xFFULL) << 56);

    gdt[base_idx + 1] = base >> 32;
}

/* Initialize the TSS for one CPU. Called once per CPU: BSP at boot,
 * and each AP after it reaches C code in Phase 2+. `kernel_stack_top`
 * is the high address of the CPU's per-CPU kernel stack; the CPU
 * switches to this stack on ring 3 -> ring 0 transitions. */
void gdt_init_cpu_tss(int cpu_id, uint64_t kernel_stack_top)
{
    if (cpu_id < 0 || cpu_id >= (int)MAX_CPUS) return;

    struct tss64 *t = &tss_array[cpu_id];
    memset(t, 0, sizeof(*t));
    t->rsp0      = kernel_stack_top;
    t->ist1      = (uint64_t)(tss_df_stack[cpu_id] + sizeof(tss_df_stack[cpu_id]));
    t->iomap_base = sizeof(*t);

    set_tss_descriptor(cpu_id, (uint64_t)(uintptr_t)t, sizeof(*t) - 1);
}

/* Return the GDT selector for CPU N's TSS. With the layout above,
 * CPU N's TSS starts at index 5 + 2*N, so the selector is
 * (5 + 2*N) * 8 = 0x28 + N*16. */
uint16_t gdt_tss_selector(int cpu_id)
{
    return (uint16_t)((GDT_FIRST_TSS_IDX + 2 * cpu_id) * 8);
}

/* Set the running CPU's kernel stack (rsp0) so that the next
 * ring 3 -> ring 0 transition lands on the right stack. */
void tss_set_rsp0(uint64_t rsp0)
{
    int cpu_id = smp_current_cpu_id();
    tss_array[cpu_id].rsp0 = rsp0;
}

void gdt_init(void)
{
    /* The fixed entries (null, kernel code/data, user code/data)
     * are installed once at boot. The per-CPU TSS descriptors are
     * installed by gdt_init_cpu_tss() — for the BSP now and for
     * each AP when it starts. */
    memset(gdt, 0, sizeof(gdt));
    gdt[0] = 0;
    gdt[1] = 0x00AF9A000000FFFFULL; /* kernel code, DPL=0, long mode */
    gdt[2] = 0x00AF92000000FFFFULL; /* kernel data, DPL=0 */
    gdt[GDT_USER_DATA_IDX] = 0x00AFF2000000FFFFULL; /* user data, DPL=3 */
    gdt[GDT_USER_CODE_IDX] = 0x00AFFA000000FFFFULL; /* user code, DPL=3, long mode */

    /* Set up the BSP's TSS. The BSP's kernel stack is the 32 KiB
     * bootstrap stack allocated in entry.S, which the BSP uses
     * until the scheduler switches to a per-task stack. We use it
     * as the initial rsp0 so the first ring 3 -> ring 0 transition
     * (e.g., a syscall from init) lands somewhere sane. */
    extern char stack_top[];
    uint64_t bsp_stack_top = (uint64_t)(uintptr_t)stack_top;
    gdt_init_cpu_tss(0, bsp_stack_top);

    const struct gdtr gdtr = {
        .limit = sizeof(gdt) - 1,
        .base = (uint64_t)(uintptr_t)gdt,
    };

    gdt_load(&gdtr, KERNEL_CODE_SELECTOR, KERNEL_DATA_SELECTOR,
             gdt_tss_selector(0));
}

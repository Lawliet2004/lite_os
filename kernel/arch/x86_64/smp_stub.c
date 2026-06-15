/*
 * smp_stub.c - AP bringup shims for the single-CPU build.
 *
 * The full SMP work lives in kernel/arch/x86_64/smp.c and
 * kernel/arch/x86_64/ap_trampoline.S. That pair brings up the BSP plus
 * any APs detected via the local APIC.
 *
 * On the single-CPU QEMU build we use, the AP trampoline is not yet
 * actually brought online (the BSP-only path in smp_init stops there).
 * However smp.c still references the trampoline symbols as `extern`
 * because the bringup paths exist for future phases. To keep the kernel
 * buildable without dragging in the 16-bit/32-bit/64-bit multi-mode
 * assembly of ap_trampoline.S, this file provides minimal stubs.
 *
 * When SMP is fully wired up, the ap_trampoline.S object can be added
 * back to ASM_SOURCES and the bodies here deleted.
 */

#include <stdint.h>

void gdt_load_cpu(int cpu_id)
{
    (void)cpu_id;
    /* BSP-only build: nothing to do. */
}

void ap_trampoline_64_continue(uint64_t cpu_id)
{
    (void)cpu_id;
    /* BSP-only build: the AP entry point is not active. */
}

/*
 * The AP bringup path uses these byte spans to memcpy the 16-bit and
 * 64-bit trampolines into a low-memory page. Without a real bringup
 * path they can be empty — their start/end pointers are only compared,
 * never dereferenced.
 */
char ap_trampoline_16_start[1] = { 0 };
char ap_trampoline_16_end[1]   = { 0 };
char ap_trampoline_64_start[1] = { 0 };
char ap_trampoline_64_end[1]   = { 0 };
char trampoline_32_start[1]    = { 0 };
char trampoline_32_end[1]      = { 0 };

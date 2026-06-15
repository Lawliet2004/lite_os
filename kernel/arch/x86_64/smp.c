/*
 * smp.c - Symmetric Multi-Processing support for LiteNix (Phase 2)
 *
 * Phase 1 added per-CPU data structures and APIC detection but left the
 * APs parked. Phase 2 brings APs online:
 *
 *   1. The BSP allocates one 4 KiB low-memory page per AP, copies the
 *      ap_trampoline.S 16/32/64-bit code into that page at fixed
 *      offsets, and fills in per-AP parameters (kernel PML4, GDT/IDT
 *      base+limit, per-CPU data pointer, kernel stack top, cpu_id,
 *      LAPIC id) at offset 0x400+.
 *
 *   2. The BSP issues the standard INIT → 10 ms → STARTUP(v) → 200 µs →
 *      STARTUP(v) → 200 µs sequence to each LAPIC ID 1..N-1.
 *      (QEMU -smp N assigns LAPIC IDs 0..N-1, so this covers all APs.)
 *
 *   3. Each AP wakes at the trampoline in 16-bit real mode, transitions
 *      to 32-bit protected mode, then to 64-bit long mode (loading the
 *      kernel's PML4 via CR3), then jumps to ap_trampoline_64_start
 *      which loads the kernel GDT/IDT/TSS, sets up a per-CPU kernel
 *      stack, installs MSR_GS_BASE, and calls smp_ap_entry() in C.
 *
 *   4. smp_ap_entry() marks g_ap_started[cpu_id] = 1 (so the BSP can
 *      stop polling), prints a confirmation message, and parks in a
 *      halt loop. Phase 4 will wire it into the per-CPU scheduler.
 */

#include <arch/x86_64/smp.h>
#include <arch/x86_64/cpu.h>
#include <arch/x86_64/io.h>
#include <arch/x86_64/limine.h>
#include <arch/x86_64/gdt.h>
#include <arch/x86_64/idt.h>
#include <arch/x86_64/syscall_entry.h>
#include <arch/x86_64/vmm.h>
#include <kernel/printk.h>
#include <kernel/panic.h>
#include <mm/pmm.h>
#include <mm/heap.h>
#include <lib/string.h>
#include <stdint.h>

/* Globals documented in smp.h */
uint64_t g_lapic_base = 0;
uint64_t g_cpu_count  = 1;
struct cpu_data g_cpu_data[MAX_CPUS] __attribute__((aligned(64)));
volatile uint32_t g_ap_started[MAX_CPUS];

bool smp_apic_supported = false;

/* ------------------------------------------------------------------ */
/* AP-side trampoline layout — see ap_trampoline.S for the asm side.    */
/* ------------------------------------------------------------------ */
#define AP_PARAM_PML4_PHYS    0x400
#define AP_PARAM_GDT_BASE     0x408
#define AP_PARAM_GDT_LIMIT    0x410
#define AP_PARAM_IDT_BASE     0x418
#define AP_PARAM_IDT_LIMIT    0x420
#define AP_PARAM_PERCPU        0x428
#define AP_PARAM_KSTACK_TOP    0x430
#define AP_PARAM_CPU_ID        0x438
#define AP_PARAM_LAPIC_ID      0x440

#define AP_16_OFFSET            0x000
#define AP_32_OFFSET            0x200
#define AP_64_OFFSET            0x300

/* The BSP-side copies of the trampoline section start/end addresses,
 * exported by ap_trampoline.S as global symbols. */
extern char ap_trampoline_16_start[];
extern char ap_trampoline_16_end[];
extern char trampoline_32_start[];
extern char trampoline_32_end[];
extern char ap_trampoline_64_start[];
extern char ap_trampoline_64_end[];

/* GDT/IDT symbols from gdt.c and idt.c (now non-static). */
extern uint64_t gdt[];
extern struct idt_entry idt[];

extern volatile struct limine_hhdm_request hhdm_request;

/* Per-AP allocation tables. The trampoline_phys is the physical address
 * of the 4 KiB page the BSP puts the trampoline in; the AP starts
 * executing there in 16-bit real mode with CS = (trampoline_phys >> 12)<<8
 * (i.e., the vector is just the page number). */
static phys_addr_t ap_trampoline_phys[MAX_CPUS];
static uint8_t  ap_kstack_storage[MAX_CPUS][16 * 1024]
    __attribute__((aligned(16)));

/* ------------------------------------------------------------------ */
/* Small CPU helpers                                                    */
/* ------------------------------------------------------------------ */

static inline void wrmsr(uint32_t msr, uint64_t value)
{
    uint32_t lo = (uint32_t)(value & 0xFFFFFFFFU);
    uint32_t hi = (uint32_t)(value >> 32);
    __asm__ volatile ("wrmsr" : : "c"(msr), "a"(lo), "d"(hi) : "memory");
}

static inline uint64_t rdmsr(uint32_t msr)
{
    uint32_t lo, hi;
    __asm__ volatile ("rdmsr" : "=a"(lo), "=d"(hi) : "c"(msr) : "memory");
    return ((uint64_t)hi << 32) | lo;
}

static inline void cpuid(uint32_t leaf,
                          uint32_t *eax, uint32_t *ebx,
                          uint32_t *ecx, uint32_t *edx)
{
    __asm__ volatile("cpuid"
                     : "=a"(*eax), "=b"(*ebx), "=c"(*ecx), "=d"(*edx)
                     : "a"(leaf), "c"(0));
}

static inline void io_delay(void)
{
    outb(0x80, 0);
}

/* ------------------------------------------------------------------ */
/* LAPIC MMIO accessors                                                  */
/* ------------------------------------------------------------------ */

uint32_t lapic_read(uint32_t reg)
{
    return *(volatile uint32_t *)(uintptr_t)(g_lapic_base + reg);
}

void lapic_write(uint32_t reg, uint32_t value)
{
    *(volatile uint32_t *)(uintptr_t)(g_lapic_base + reg) = value;
}

bool lapic_supported(void)
{
    return smp_apic_supported;
}

static uint32_t detect_lapic_id(void)
{
    uint32_t eax, ebx, ecx, edx;
    cpuid(1, &eax, &ebx, &ecx, &edx);
    return (ebx >> 24) & 0xFF;
}

static bool detect_apic(void)
{
    uint32_t eax, ebx, ecx, edx;
    cpuid(1, &eax, &ebx, &ecx, &edx);
    return (edx & (1u << 9)) != 0;
}

static uint64_t read_lapic_base_msr(void)
{
    return rdmsr(MSR_IA32_APIC_BASE) & 0xFFFFF000ULL;
}

static void lapic_enable(void)
{
    uint32_t svr = lapic_read(LAPIC_REG_SVR);
    svr |= (1u << 8);
    svr &= 0xFFFFFF00u;
    svr |= 0xFF;
    lapic_write(LAPIC_REG_SVR, svr);

    lapic_write(LAPIC_REG_LVT_TIMER,   0x10000);
    lapic_write(LAPIC_REG_LVT_THERMAL, 0x10000);
    lapic_write(LAPIC_REG_LVT_PMI,     0x10000);
    lapic_write(LAPIC_REG_LVT_LINT0,   0x10000);
    lapic_write(LAPIC_REG_LVT_LINT1,   0x10000);
    lapic_write(LAPIC_REG_LVT_ERROR,   0x10000);

    lapic_write(LAPIC_REG_TPR, 0);
}

/* Phase 3d: the per-CPU IPI delivery counters used by the BSP-side
 * self-test and the C-side IPI handlers. These are written from IPI
 * handlers (with IRQs disabled) and read from normal kernel context,
 * so they are volatile. Each counter is a uint32 so a single aligned
 * 32-bit load/store is atomic on x86. */
volatile uint32_t g_ipi_count_reschedule    = 0;
volatile uint32_t g_ipi_count_tlb_shootdown = 0;
volatile uint32_t g_ipi_count_panic         = 0;

/* Phase 5: per-CPU TLB-shootdown queues. Aligned to 64 to keep
 * producer (sender) and consumer (this CPU) on separate cache
 * lines; the IPI handler reads tail on this CPU and the BSP writes
 * head+entries from its own CPU. The IPI itself is the
 * happens-before barrier. */
struct tlb_shootdown_queue g_tlb_queue[MAX_CPUS]
    __attribute__((aligned(64)));

void tlb_shootdown_publish_all_others(uint64_t virt)
{
    int self = smp_current_cpu_id();
    for (int i = 0; i < (int)g_cpu_count; i++) {
        if (i == self) continue;
        struct tlb_shootdown_queue *q = &g_tlb_queue[i];
        uint32_t h = q->head;
        q->entries[h % TLB_QUEUE_PER_CPU].vaddr = virt;
        /* The IPI is the synchronization point: the receiver won't
         * see head update until the IPI is delivered, and we won't
         * send the IPI until after this barrier. The compiler
         * barrier here just stops the compiler from sinking the
         * entry store below the head store. */
        __asm__ volatile ("" ::: "memory");
        q->head = h + 1;
    }
}

void tlb_shootdown_drain(void)
{
    int id = smp_current_cpu_id();
    if (id < 0 || id >= MAX_CPUS) return;
    struct tlb_shootdown_queue *q = &g_tlb_queue[id];
    while (q->tail != q->head) {
        uint32_t t = q->tail;
        uint64_t v = q->entries[t % TLB_QUEUE_PER_CPU].vaddr;
        __asm__ volatile ("invlpg (%0)" : : "r"(v) : "memory");
        q->tail = t + 1;
    }
}

void tlb_shootdown_self_test(void)
{
    printk("TLB: self-test starting\n");

    struct address_space *ks = vmm_kernel_address_space();
    phys_addr_t phys = pmm_alloc_page();
    if (phys == 0) panic("TLB: self-test PMM alloc failed");

    virt_addr_t test_virt = 0xCAFE0000ULL;
    if (vmm_map(ks, test_virt, phys, VMM_PRESENT | VMM_WRITABLE) != 0) {
        panic("TLB: self-test map failed");
    }

    /* Touch it so there's something in the TLB to invalidate. */
    volatile uint8_t *p = (volatile uint8_t *)test_virt;
    (void)*p;

    int id = smp_current_cpu_id();
    struct tlb_shootdown_queue *q = &g_tlb_queue[id];
    uint32_t head_before = q->head;
    uint32_t tail_before = q->tail;

    /* Manually enqueue one entry into the BSP's own slot (normally
     * the BSP would publish to *other* CPUs; for the self-test we
     * want the BSP to also exercise the drain path). */
    q->entries[head_before % TLB_QUEUE_PER_CPU].vaddr = test_virt;
    __asm__ volatile ("" ::: "memory");
    q->head = head_before + 1;

    if (q->head != head_before + 1) {
        panic("TLB: self-test head did not advance");
    }
    if (q->tail != tail_before) {
        panic("TLB: self-test tail changed unexpectedly");
    }

    tlb_shootdown_drain();
    if (q->head != q->tail) {
        panic("TLB: self-test single-entry drain failed (head=%u tail=%u)",
              (unsigned)q->head, (unsigned)q->tail);
    }

    /* Publish a burst of 4 entries, drain, verify. */
    for (int i = 0; i < 4; i++) {
        uint32_t h = q->head;
        q->entries[h % TLB_QUEUE_PER_CPU].vaddr = test_virt;
        __asm__ volatile ("" ::: "memory");
        q->head = h + 1;
    }
    tlb_shootdown_drain();
    if (q->head != q->tail) {
        panic("TLB: self-test multi-entry drain failed (head=%u tail=%u)",
              (unsigned)q->head, (unsigned)q->tail);
    }

    vmm_unmap(ks, test_virt);
    pmm_free_page(phys);

    printk("TLB: self-test passed (5 entries drained)\n");
}

/* Phase 6 self-test: exercise the cross-CPU reschedule IPI path
 * without needing live APs. We fake a second CPU with a non-existent
 * LAPIC ID and call smp_send_ipi_to_cpu(1, IPI_VECTOR_RESCHEDULE).
 * The IPI may not actually deliver (LAPIC 0xFE doesn't exist), but
 * the call must return — that's the bounded-wait guarantee. */
void reschedule_self_test(void)
{
    printk("Sched: reschedule IPI self-test starting\n");

    uint64_t saved_count = g_cpu_count;
    uint32_t saved_lapic = (uint32_t)g_cpu_data[1].lapic_id;
    g_cpu_count = 2;
    g_cpu_data[1].lapic_id = 0xFE;  /* non-existent LAPIC */

    smp_send_ipi_to_cpu(1, IPI_VECTOR_RESCHEDULE);

    g_cpu_count = saved_count;
    g_cpu_data[1].lapic_id = saved_lapic;

    printk("Sched: reschedule IPI self-test passed (bounded wait did not hang)\n");
}

/* Phase 32 followup self-test: exercise smp_broadcast_panic() with
 * a fake second CPU whose LAPIC doesn't exist. The IPI goes out
 * to "all excluding self" (a 0-target broadcast in single-CPU
 * mode, or a 1-target broadcast in single-CPU mode if we fake
 * CPU 1 with a non-existent LAPIC), and must not hang the BSP.
 * Mirrors reschedule_self_test(). */
void panic_broadcast_self_test(void)
{
    printk("Panic: broadcast self-test starting\n");

    uint64_t saved_count = g_cpu_count;
    uint32_t saved_lapic = (uint32_t)g_cpu_data[1].lapic_id;
    g_cpu_count = 2;
    g_cpu_data[1].lapic_id = 0xFE;  /* non-existent LAPIC */

    smp_broadcast_panic();

    g_cpu_count = saved_count;
    g_cpu_data[1].lapic_id = saved_lapic;

    printk("Panic: broadcast self-test passed (bounded wait did not hang)\n");
}

/* Forward decl: smp_handle_ipi is defined further down; the
 * self-test uses it to invoke a specific IPI handler in
 * isolation (without needing the LAPIC to actually deliver). */
void smp_handle_ipi(uint32_t vector);

/* Phase 32 followup self-test: invoke smp_handle_ipi() directly
 * for the reschedule and TLB-shootdown paths. The IPI never
 * actually arrives in single-CPU mode (smp_send_ipi_to_cpu
 * skips self, and the broadcast shorthand has no targets in
 * single-CPU mode), so we test the handler logic in isolation
 * here. When APs come up, the same handler will run for real
 * on the receiving CPU.
 *
 * Reschedule: verify the per-CPU need_resched flag is set and
 * the IPI counter is bumped.
 *
 * TLB-shootdown: verify the per-CPU TLB queue is drained and the
 * IPI counter is bumped. (We can't verify the invlpg issued, but
 * the queue is what carries the work.) */
void ipi_handler_self_test(void)
{
    printk("IPI: handler self-test starting\n");

    int cpu = smp_current_cpu_id();
    if (cpu < 0 || cpu >= MAX_CPUS) {
        printk("IPI: handler self-test SKIPPED (bad cpu id)\n");
        return;
    }

    /* --- Reschedule path --- */
    uint32_t saved_resched = (uint32_t)g_cpu_data[cpu].need_resched;
    uint32_t before = g_ipi_count_reschedule;
    g_cpu_data[cpu].need_resched = 0;
    smp_handle_ipi(IPI_VECTOR_RESCHEDULE);
    if (g_cpu_data[cpu].need_resched != 1) {
        panic("IPI: reschedule handler did not set per-CPU need_resched");
    }
    if (g_ipi_count_reschedule != before + 1) {
        panic("IPI: reschedule handler did not bump counter");
    }
    g_cpu_data[cpu].need_resched = saved_resched;

    /* --- TLB-shootdown path --- */
    /* Manually populate the BSP's per-CPU TLB queue (the publish
     * helper excludes self, so we can't use it in single-CPU mode).
     * The handler should drain the queue (head == tail) and bump
     * the counter. (We can't verify the invlpg issued, but the
     * queue is what carries the work.) */
    uint32_t before_tlb = g_ipi_count_tlb_shootdown;
    int tcpu = smp_current_cpu_id();
    if (tcpu < 0 || tcpu >= MAX_CPUS) {
        printk("IPI: handler self-test SKIPPED (bad cpu id for TLB)\n");
        return;
    }
    struct tlb_shootdown_queue *q = &g_tlb_queue[tcpu];
    q->head = 0;
    q->tail = 0;
    q->entries[0].vaddr = 0x100000ULL;
    q->entries[1].vaddr = 0x101000ULL;
    q->head = 2;  /* 2 pending */
    smp_handle_ipi(IPI_VECTOR_TLB_SHOOTDOWN);
    if (q->head != q->tail) {
        panic("IPI: TLB handler did not drain the queue (head=%u tail=%u)",
              (unsigned)q->head, (unsigned)q->tail);
    }
    if (g_ipi_count_tlb_shootdown != before_tlb + 1) {
        panic("IPI: TLB handler did not bump counter");
    }

    /* --- Panic path NOT tested: would halt. --- */

    printk("IPI: handler self-test passed (reschedule + TLB-shootdown handlers verified)\n");
}

/* Phase 32 followup self-test: pure-vector entry point for the
 * IPI handlers. Extracted from ipi_dispatch() so a self-test can
 * invoke a specific IPI handler in isolation (without needing
 * the IPI to actually arrive via the LAPIC — which is blocked
 * by QEMU's LAPIC SVR=0 bug in the single-CPU test path).
 * IRQs are disabled on entry (matching the real IPI dispatch). */
void smp_handle_ipi(uint32_t vector)
{
    switch (vector) {
    case IPI_VECTOR_RESCHEDULE:
        g_ipi_count_reschedule++;
        {
            int rcpu = smp_current_cpu_id();
            if (rcpu >= 0 && rcpu < MAX_CPUS) {
                g_cpu_data[rcpu].need_resched = 1;
            }
        }
        break;
    case IPI_VECTOR_TLB_SHOOTDOWN:
        g_ipi_count_tlb_shootdown++;
        tlb_shootdown_drain();
        break;
    case IPI_VECTOR_PANIC:
        g_ipi_count_panic++;
        for (;;) cpu_halt();
        break;
    default:
        break;
    }
}

/* C-side IPI dispatcher. Called from isr_ipi_common (in isr.S)
 * with the saved register state on the stack. We decode the vector
 * and call smp_handle_ipi. IRQs are disabled on entry. */
void ipi_dispatch(struct interrupt_frame *frame)
{
    smp_handle_ipi(frame->vector);
}

static void lapic_send_ipi(uint32_t dst_lapic, uint32_t vector, uint32_t mode)
{
    /* Bound: QEMU 8.2.2's LAPIC can leave delivery_status set
     * forever when the target can't accept the IPI (e.g., broadcast
     * to APs with LAPIC SVR=0). 10000 PAUSE spins ≈ 1 ms, plenty
     * for any real LAPIC delivery. */
    for (int _s = 0; _s < 10000 && (lapic_read(LAPIC_REG_ICR_LOW) & (1u << 12)); _s++) {
        __asm__ volatile ("pause");
    }

    if (mode == ICR_DEST_ALL_EXCL_SELF || mode == ICR_DEST_ALL) {
        lapic_write(LAPIC_REG_ICR_HIGH, 0);
        lapic_write(LAPIC_REG_ICR_LOW, mode | vector);
    } else {
        lapic_write(LAPIC_REG_ICR_HIGH, (dst_lapic & 0xFF) << 24);
        lapic_write(LAPIC_REG_ICR_LOW, mode | vector);
    }

    /* Same bound for the post-send wait. If the IPI didn't deliver
     * (missing target), we return; the caller's counter self-test
     * will see no increment. */
    for (int _s = 0; _s < 10000 && (lapic_read(LAPIC_REG_ICR_LOW) & (1u << 12)); _s++) {
        __asm__ volatile ("pause");
    }
}

void smp_send_ipi(uint32_t lapic_id, uint32_t vector)
{
    lapic_send_ipi(lapic_id, vector, ICR_DEST_FIELD | ICR_FIXED | ICR_ASSERT);
}

void smp_broadcast_ipi_excluding_self(uint32_t vector)
{
    lapic_send_ipi(0, vector, ICR_DEST_ALL_EXCL_SELF | ICR_FIXED | ICR_ASSERT);
}

void smp_send_ipi_to_cpu(int cpu_id, uint32_t vector)
{
    if (cpu_id < 0 || cpu_id >= (int)g_cpu_count) return;
    if (cpu_id == smp_current_cpu_id()) return;
    smp_send_ipi((uint32_t)g_cpu_data[cpu_id].lapic_id, vector);
}

/* Phase 32 followup #3: send an IPI to the calling CPU. Unlike
 * smp_send_ipi_to_cpu() (which skips self) and the broadcast
 * shorthands, this actually delivers the IPI back to the
 * caller. The BSP's LAPIC SVR is enabled (0x1FF) so the IPI is
 * accepted and dispatched to the isr_ipi_common stub. Useful
 * for self-tests that need to exercise the IPI receive path
 * without live APs. */
void smp_self_ipi(uint32_t vector)
{
    int cpu = smp_current_cpu_id();
    if (cpu < 0 || cpu >= MAX_CPUS) return;
    smp_send_ipi((uint32_t)g_cpu_data[cpu].lapic_id, vector);
}

void smp_halt_cpu(int cpu_id)
{
    if (cpu_id < 0 || cpu_id >= (int)g_cpu_count) return;
    if (cpu_id == smp_current_cpu_id()) return;
    g_cpu_data[cpu_id].ap_should_halt = 1;
    smp_send_ipi((uint32_t)g_cpu_data[cpu_id].lapic_id, IPI_VECTOR_PANIC);
}

/* Phase 32 followup: broadcast a halt request to every CPU other
 * than the caller. Used by panic() so a panicking CPU tells its
 * peers to halt before it enters the cli/hlt loop. The IPI
 * handler on the receiving side enters its own halt loop
 * (see ipi_dispatch IPI_VECTOR_PANIC). */
void smp_broadcast_panic(void)
{
    /* The broadcast shorthand "all excluding self" with a
     * missing/disabled target won't deadlock the sender: the
     * bounded wait in lapic_send_ipi() returns after 10 000
     * PAUSE spins (~1 ms), and the panicking CPU is going to
     * halt anyway. In single-CPU mode this is a no-op (no
     * targets); in multi-CPU mode the APs receive the IPI and
     * halt. */
    smp_broadcast_ipi_excluding_self(IPI_VECTOR_PANIC);
}

/* ------------------------------------------------------------------ */
/* CPU-ID helpers                                                        */
/* ------------------------------------------------------------------ */

int smp_current_cpu_id(void)
{
    uint64_t gs_base = rdmsr(MSR_GS_BASE);
    if (gs_base == 0) return 0;

    int cpu_id;
    __asm__ volatile ("mov 24(%1), %0" : "=r"(cpu_id) : "r"(gs_base) : "memory");
    return cpu_id;
}

/* Phase 7: the PMM calls this in pmm_init (before the heap runs)
 * to know how many low-memory pages to pre-reserve for AP
 * trampolines. We cap at MAX_CPUS-1 because the BSP doesn't need
 * a trampoline. The number is also capped at 4, which is what
 * SMP_TRY_APS is set to in the bring-up loop; this keeps the
 * reserved pool from being bigger than the number of APs we'll
 * actually try. */
int smp_max_trampoline_pages(void)
{
    int n = 4;  /* matches SMP_TRY_APS in smp_boot_aps */
    if (n > (int)MAX_CPUS - 1) n = (int)MAX_CPUS - 1;
    return n;
}

struct cpu_data *smp_current_cpu(void)
{
    uint64_t gs_base = rdmsr(MSR_GS_BASE);
    if (gs_base == 0) return &g_cpu_data[0];
    return (struct cpu_data *)(uintptr_t)gs_base;
}

struct cpu_data *smp_cpu_data(int cpu_id)
{
    if (cpu_id < 0 || cpu_id >= (int)g_cpu_count) return 0;
    return &g_cpu_data[cpu_id];
}

/* The scheduler and task subsystems have ~60 references to a global
 * `current_task` pointer. For Phase 3c we keep that pointer as the
 * authoritative store for the BSP and treat the per-CPU
 * `current_task` (at offset 40 in cpu_data) as a shadow that the
 * scheduler keeps in sync via these helpers. When Phase 2's AP
 * bring-up actually lands, each AP will write to its own slot
 * and the global becomes just the BSP's view. */
struct task *smp_get_current_task(void)
{
    struct cpu_data *c = smp_current_cpu();
    if (c == 0) return 0;
    return (struct task *)(uintptr_t)c->current_task;
}

void smp_set_current_task(struct task *t)
{
    struct cpu_data *c = smp_current_cpu();
    if (c == 0) return;
    c->current_task = (uint64_t)(uintptr_t)t;
}

/* ------------------------------------------------------------------ */
/* AP-side C entry (called from ap_trampoline_64 in long mode)            */
/* ------------------------------------------------------------------ */

void smp_ap_entry(void)
{
    /* Phase 7 diagnostic: the first thing the AP does in C is mark
     * the C-entry diag byte at trampoline offset 0xF14. If the BSP
     * doesn't see this marker set after the bring-up timeout, the
     * call to smp_ap_entry was never made (the 64-bit code stalled
     * before the call instruction). The BSP reads this offset via
     * the HHDM after polling g_ap_started[cpu_id] times out.
     *
     * We use a raw 0xF14 here instead of going through the param
     * block because the AP's MSR_GS_BASE is already installed and
     * any 0xF14 write is a direct memory store (no scratch
     * registers needed). The kernel PML4's Limine-installed low
     * half mapping puts 0xF14 in the same 2 MiB page as the
     * trampoline. */
    *(volatile uint8_t *)(uintptr_t)0xF14 = 0x14;  /* AP_DIAG_C_ENTRY */

    int cpu_id = smp_current_cpu_id();
    if (cpu_id < 0 || cpu_id >= (int)g_cpu_count) {
        panic("SMP: AP booted with invalid cpu_id");
    }

    struct cpu_data *cpu = &g_cpu_data[cpu_id];
    printk("SMP: AP %d (LAPIC %u) reached C entry\n",
           cpu_id, (unsigned)cpu->lapic_id);

    g_ap_started[cpu_id] = 1;

    interrupts_enable();
    for (;;) {
        cpu_halt();
        if (cpu->ap_should_halt) break;
    }
    for (;;) cpu_halt();
}

/* ------------------------------------------------------------------ */
/* PIT-based delay for IPIs                                               */
/* ------------------------------------------------------------------ */

static void delay_ms(uint32_t ms)
{
    /* PIT channel 2 in mode 0 (interrupt on terminal count), gated on.
     * The PIT input clock is 1.193182 MHz, so for N ms we count 1193*N. */
    uint32_t count = ms * 1193;
    if (count == 0) count = 1;
    if (count > 0xFFFF) count = 0xFFFF;

    outb(0x43, 0xB0);
    outb(0x42, (uint8_t)(count & 0xFF));
    outb(0x42, (uint8_t)(count >> 8));

    uint8_t mask = inb(0x61) & 0xFC;
    outb(0x61, (uint8_t)(mask | 0x01));
    io_delay();

    for (;;) {
        outb(0x43, 0x80);
        uint8_t status = inb(0x40);
        if ((status & 0x80) != 0) break;
    }
    outb(0x61, mask);
}

static void delay_us(uint32_t us) __attribute__((unused));
static void delay_us(uint32_t us)
{
    /* Sub-millisecond delay: busy loop with PAUSE. Calibrated loosely
     * for QEMU's TCG clock; Phase 5 can switch to TSC-accurate. */
    volatile uint64_t spins = (uint64_t)us * 8;
    while (spins--) {
        __asm__ volatile ("pause");
    }
}

/* ------------------------------------------------------------------ */
/* AP bring-up: trampoline + INIT-SIPI-SIPI per LAPIC id                  */
/* ------------------------------------------------------------------ */

/* Build a PTE entry for a page table or page directory pointer. The
 * vmm module's make_entry() is `static`, so we have our own here. We
 * only need a small subset: PRESENT|WRITABLE for intermediate tables
 * and PRESENT|WRITABLE for the trampoline's leaf page.
 *
 * DISABLED: the function is unused after we stubbed out
 * map_trampoline_page(). Kept as a no-op so the file still links if
 * the trampoline code comes back online. */
__attribute__((unused))
static uint64_t smp_make_pte(phys_addr_t phys, uint64_t flags)
{
    return (phys & 0x000FFFFFFFFFF000ULL) | (flags & 0x8000000000000FFFULL);
}

/* Install a 4 KiB identity-mapping for `phys` in the kernel PML4 by
 * overwriting PML4[0] with a fresh PDPT/PD/PT chain. The chain is
 * allocated from the PMM. Only the page at `phys` (and the tables
 * themselves) is mapped; the rest of the low half is left
 * unaddressed, which is fine because nothing else needs the low
 * half once the AP is up.
 *
 * Disabled: overwriting PML4[0] destroys the Limine-installed
 * mapping for the LAPIC (0xfee00000), which the BSP needs
 * right after sending the SIPI to read the ICR delivery
 * status. The PML4 fix would need to merge our chain with
 * Limine's, which is non-trivial. Since the AP still fails
 * to reach C with the *original* Limine chain (the deeper
 * reason is something else — Phase 5), the simpler path
 * is to leave the Limine PML4 in place and skip this map. */
__attribute__((unused))
static void map_trampoline_page(phys_addr_t phys)
{
    (void)phys;
}

/* Set up the trampoline page for one AP. The BSP fills in all the
 * parameters the AP needs to enter 64-bit long mode and then load the
 * kernel's GDT/IDT/TSS and call smp_ap_entry() in C. */
static void setup_ap_trampoline(int cpu_id, uint8_t lapic_id)
{
    /* Allocate the 4 KiB trampoline page in low memory. The SIPI vector
     * field is the page number, so this address must be < 1 MiB.
     * We use a dedicated allocator that walks the Limine memory map
     * directly, because by the time smp_init runs the heap and VFS
     * have already consumed all the pages in the PMM's bitmap
     * view of the first 1 MiB. */
    phys_addr_t phys = pmm_alloc_trampoline_page(0x100000);
    if (phys == 0) {
        /* For diagnostics, dump the Limine memory map and the bitmap
         * state for the first 0x100 pages. */
        volatile struct limine_memmap_response *mm = memmap_request.response;
        printk("SMP: low-memory page alloc failed; limine memmap (%llu entries):\n",
               mm != 0 ? (unsigned long long)mm->entry_count : 0ULL);
        for (uint64_t i = 0; mm != 0 && i < mm->entry_count; i++) {
            const struct limine_memmap_entry *e = mm->entries[i];
            printk("  [%llu] base=0x%llx len=0x%llx type=%llu\n",
                   (unsigned long long)i,
                   (unsigned long long)e->base,
                   (unsigned long long)e->length,
                   (unsigned long long)e->type);
        }
        printk("SMP: low-mem bitmap scan (first 0x100 pages):\n");
        for (uint64_t i = 0x60; i < 0x100; i++) {
            printk("  page 0x%llx phys=0x%llx\n",
                    (unsigned long long)i,
                    (unsigned long long)(i * 0x1000));
        }
        /* No low-memory page is available — the QEMU memory map does
         * not include any usable region below 1 MiB. AP bringup needs
         * that region for the 16-bit→64-bit trampoline (the SIPI vector
         * field can only address the first 1 MiB). Degrade gracefully:
         * we keep g_cpu_count = 1 and exit cleanly. */
        printk("SMP: no low-memory page available for AP %d — AP bringup disabled\n",
               cpu_id);
        return;
    }
    if (phys > 0x100000) {
        panic("SMP: trampoline for AP %d at 0x%llx is above 1 MiB "
              "(SIPI vector field can't address it)", cpu_id,
              (unsigned long long)phys);
    }
    ap_trampoline_phys[cpu_id] = phys;

    /* Get a virtual pointer to the page (via HHDM) and clear it. */
    void *virt = (void *)(uintptr_t)(hhdm_request.response->offset + phys);
    memset(virt, 0, 4096);

    /* Copy the three trampoline sections into their hard-coded offsets
     * within the page. See ap_trampoline.S for the layout. */
    uint64_t s16 = (uint64_t)(ap_trampoline_16_end - ap_trampoline_16_start);
    uint64_t s32 = (uint64_t)(trampoline_32_end - trampoline_32_start);
    uint64_t s64 = (uint64_t)(ap_trampoline_64_end - ap_trampoline_64_start);

    if (s16 > AP_32_OFFSET) panic("SMP: 16-bit trampoline too large");
    if (s32 > AP_64_OFFSET - AP_32_OFFSET) panic("SMP: 32-bit trampoline too large");
    if (s64 > AP_PARAM_PML4_PHYS - AP_64_OFFSET) panic("SMP: 64-bit trampoline too large");

    memcpy((uint8_t *)virt + AP_16_OFFSET, ap_trampoline_16_start, s16);
    memcpy((uint8_t *)virt + AP_32_OFFSET, trampoline_32_start, s32);
    memcpy((uint8_t *)virt + AP_64_OFFSET, ap_trampoline_64_start, s64);

    /* Compute the per-AP kernel stack top. The stack grows down, so the
     * top is the highest address of the storage we reserved. */
    uint64_t kstack_top = (uint64_t)(uintptr_t)&ap_kstack_storage[cpu_id][sizeof(ap_kstack_storage[cpu_id])];

    /* Compute the GDT and IDT limits. The kernel's GDT has 7 entries
     * (see gdt.c) and the IDT has 256 entries (see idt.c). The struct
     * sizes are 8 bytes and 16 bytes respectively. */
    const uint16_t gdt_limit = (uint16_t)(7 * 8 - 1);
    const uint16_t idt_limit = (uint16_t)(256 * 16 - 1);

    /* Set up the parameter block at offset 0x400+ */
    uint64_t pml4_phys = vmm_kernel_address_space()->pml4_phys;
    *(uint64_t *)((uint8_t *)virt + AP_PARAM_PML4_PHYS) = pml4_phys;
    *(uint64_t *)((uint8_t *)virt + AP_PARAM_GDT_BASE)  = (uint64_t)(uintptr_t)gdt;
    *(uint16_t *)((uint8_t *)virt + AP_PARAM_GDT_LIMIT) = gdt_limit;
    *(uint64_t *)((uint8_t *)virt + AP_PARAM_IDT_BASE)  = (uint64_t)(uintptr_t)idt;
    *(uint16_t *)((uint8_t *)virt + AP_PARAM_IDT_LIMIT) = idt_limit;
    *(uint64_t *)((uint8_t *)virt + AP_PARAM_PERCPU)    = (uint64_t)(uintptr_t)&g_cpu_data[cpu_id];
    *(uint64_t *)((uint8_t *)virt + AP_PARAM_KSTACK_TOP) = kstack_top;
    *(uint64_t *)((uint8_t *)virt + AP_PARAM_CPU_ID)     = (uint64_t)cpu_id;
    *(uint32_t *)((uint8_t *)virt + AP_PARAM_LAPIC_ID)   = (uint32_t)lapic_id;

    /* Phase 7: write a known pattern to a "BSP canary" offset in the
     * diag region (0xF80, beyond the markers but still in the diag
     * page). After the SIPI timeout, the BSP reads this byte back to
     * confirm the diag read path is working. If the canary is missing
     * but the 16-bit code's 0xF00 marker is also missing, we know the
     * diag read itself is broken; if the canary is present but the
     * 16-bit marker is missing, the AP never started executing the
     * trampoline (a SIPI or AP-initialization failure). */
    *(volatile uint8_t *)((uint8_t *)virt + 0xF80) = 0xAA;  /* AP_DIAG_BSP_CANARY */

    /* Also record the LAPIC id in the BSP-side per-CPU table so the
     * "AP N online" reporting loop can match the APs we are trying to
     * start. (The AP will overwrite this same slot with its own data
     * once it reaches C, but the field is set early so the BSP has
     * something to display before the AP is up.) */
    g_cpu_data[cpu_id].lapic_id = lapic_id;

    /* Phase 4: install a fresh page-table chain for the trampoline.
     *
     * Why we don't use vmm_map here:
     *   Limine's PML4[0] chain is built for the bootloader's needs and
     *   uses 2 MiB pages in the PD, not 4 KiB PTs. vmm_map's
     *   walk_to_pte() descends PML4 -> PDPT -> PD -> PT, and would
     *   treat Limine's 2 MiB PD entry as a present "PT", reading
     *   PT[0x69] from random bytes. The result is that vmm_map
     *   either fails with EEXIST or, worse, scribbles on a random
     *   page of memory. The right fix is to throw away Limine's
     *   low-half mapping and install our own, dedicated to the
     *   trampoline's 4 KiB. The kernel doesn't need anything else
     *   in the low half: high-half (kernel text, HHDM) is fully
     *   mapped by Limine. The trampoline is the only thing we'll
     *   ever run from physical < 1 MiB after the bootloader is
     *   done.
     *
     * Disabled: see map_trampoline_page() for why PML4[0] overwrites
     * destroy the LAPIC mapping. */
    (void)phys;
}

/* Phase 4: tear down the trampoline's PML4 mapping once the AP is
 * up. Called once per LAPIC ID by the boot orchestrator after the
 * AP-side handshake completes (or after the bring-up timeout).
 *
 * The mapping we installed via map_trampoline_page() overwrites
 * Limine's PML4[0] entry. We don't have a clean way to undo that
 * without re-installing Limine's PML4[0] (which we never saved),
 * so we leave the new chain in place. The new chain is small
 * (4 pages per AP) and covers exactly the trampoline. Future
 * work that needs Limine's low-half mapping back would have to
 * re-read the Limine memmap and rebuild. For now, only the BSP
 * uses the low half (and only for the trampoline), and the BSP
 * never runs that chain after the APs are up. */
static void cleanup_ap_trampoline(int cpu_id)
{
    (void)cpu_id;
}

/* Send INIT + STARTUP IPIs to all APs simultaneously via the
 * broadcast shorthands. This is the Phase 7 alternative to
 * per-LAPIC ID targeting; it works because in QEMU -smp 4 the
 * LAPIC IDs are 0,1,2,3 and we can boot all three APs with a
 * single IPI each. We rely on the BSP's LAPIC accepting the
 * broadcast and forwarding to all AP LAPICs. The APs each get
 * their own trampoline parameter block from the pre-reserved pool
 * via setup_ap_trampoline(), so the parameters aren't shared
 * across APs.
 *
 * Sequence:
 *   - INIT(assert)         ;  50 ms
 *   - STARTUP(v)           ;  1 ms
 *   - STARTUP(v)           ;  1 ms
 */
__attribute__((unused))
static void boot_aps_broadcast(void)
{
    /* Broadcast INIT to all APs (excluding self). QEMU supports
     * ICR_DEST_ALL_EXCL_SELF for both INIT and STARTUP. */
    lapic_send_ipi(0, 0, ICR_DEST_ALL_EXCL_SELF | ICR_INIT | ICR_ASSERT);
    delay_ms(50);

    /* Broadcast STARTUP with vector = 0x7b (the first trampoline
     * page; all APs share the same vector but their parameters
     * differ per-CPU). The same vector goes to every AP — that's
     * fine because the SIPI only sets the start address; each AP
     * reads its own parameter block at its own trampoline
     * address. */
    extern char stack_top[];
    (void)stack_top;  /* silence unused warning if compiler complains */
    uint32_t vector = (uint32_t)ap_trampoline_phys[1] >> 12;  /* AP 1's vector */
    lapic_send_ipi(0, vector, ICR_DEST_ALL_EXCL_SELF | ICR_STARTUP | ICR_ASSERT);
    delay_ms(1);
    lapic_send_ipi(0, vector, ICR_DEST_ALL_EXCL_SELF | ICR_STARTUP | ICR_ASSERT);
    delay_ms(1);
}

/* Send INIT + STARTUP IPIs to one AP, addressed to *its* vector.
 * Used in the per-LAPIC fallback when broadcast SIPI isn't viable
 * (e.g., when the APs need to read different 0xE0 mailbox values
 * because the trampolines are at different physical addresses).
 * The sequence is INIT(assert) → 50 ms → STARTUP(v) → 1 ms →
 * STARTUP(v) → 1 ms. */
__attribute__((unused))
static void boot_one_ap(int cpu_id, uint8_t lapic_id)
{
    phys_addr_t phys = ap_trampoline_phys[cpu_id];
    uint32_t vector = (uint32_t)(phys >> 12);

    printk("SMP: booting AP %d (LAPIC %u, trampoline=0x%llx, vector=0x%x)\n",
           cpu_id, (unsigned)lapic_id,
           (unsigned long long)phys, (unsigned)vector);

    lapic_send_ipi(lapic_id, 0, ICR_DEST_FIELD | ICR_INIT | ICR_ASSERT);
    delay_ms(50);
    lapic_send_ipi(lapic_id, vector, ICR_DEST_FIELD | ICR_STARTUP | ICR_ASSERT);
    delay_ms(1);
    lapic_send_ipi(lapic_id, vector, ICR_DEST_FIELD | ICR_STARTUP | ICR_ASSERT);
    delay_ms(1);
}

/* Try to boot LAPIC IDs 1..N-1 as APs. QEMU -smp N assigns LAPIC IDs
 * 0..N-1 to the N vCPUs. We don't have a portable way to discover the
 * actual number of APs from inside the guest yet (a Phase 5 task is to
 * parse the ACPI MADT). For QEMU + Limine the safe default is to
 * try all of them and rely on the per-AP timeout to skip non-existent
 * ones — but the per-AP stall is ~1 second, so 63 iterations would
 * take a minute. Cap the loop at SMP_TRY_APS to keep boot fast. */
#define SMP_TRY_APS 4
static void smp_boot_aps(void)
{
    uint32_t bsp_lapic = (uint32_t)g_cpu_data[0].lapic_id;

    int max_try = SMP_TRY_APS;
    if (max_try > (int)MAX_CPUS - 1) max_try = (int)MAX_CPUS - 1;

    /* Phase 7: set up each AP's individual trampoline (the 4 KiB
     * page with its 16/32/64-bit code and parameter block) BEFORE
     * sending any IPIs. The BSP-side pre-reserved pool of low-memory
     * pages (from pmm_init) gives us 4 distinct physical addresses,
     * one per AP. */
    for (int cpu_id = 1; cpu_id <= max_try; cpu_id++) {
        uint8_t lapic_id = (uint8_t)cpu_id;
        if (lapic_id == bsp_lapic) continue;  /* BSP is not an AP */
        setup_ap_trampoline(cpu_id, lapic_id);
    }

    /* Phase 8: per-LAPIC targeted INIT + STARTUP. The previous
     * broadcast approach had a bug: all APs start at the same
     * vector and would all read the same parameter block, so
     * AP 2-4 would actually run with AP 1's parameters. The
     * per-LAPIC version sends each AP its own vector so each
     * lands on its own trampoline. The 16-bit code reads the
     * trampoline address from a 0xE0 mailbox that the BSP writes
     * to just before each SIPI. */
    /* Phase 8 (attempt 2): broadcast INIT + STARTUP. The per-LAPIC
     * targeted approach leaves AP #1 in EIP=0x000fd0a9 HLT=1 even
     * after the SIPI, so the QEMU "wait for SIPI" code's SIPI
     * delivery doesn't fire. QEMU 8.2.2's per-LAPIC STARTUP
     * delivery appears to be broken; broadcast works because
     * QEMU shortcuts the per-CPU dispatch.
     *
     * Trade-off: all APs start at the same vector (AP 1's
     * trampoline = 0x7b000) and run the same code with the same
     * parameter block. The mailbox holds AP 1's address, so all
     * APs end up using AP 1's per-CPU data. The scheduler hasn't
     * been made per-CPU yet, so concurrent use of `current_task`
     * is still a single-CPU design. This is intentional: it gets
     * us *any* AP online so we can debug the rest of the path.
     * Phase 9 will switch to per-LAPIC once we understand the
     * QEMU issue. */
    (void)bsp_lapic;  /* suppress unused warning */
    {
        phys_addr_t phys = ap_trampoline_phys[1];  /* vector = phys >> 12 */
        if (phys == 0) {
            printk("SMP: no AP trampoline reserved, skipping AP bringup\n");
        } else {
            *(volatile uint32_t *)((uintptr_t)0x9F000) = (uint32_t)phys;
            __asm__ volatile ("mfence" ::: "memory");
            boot_aps_broadcast();
        }
    }

    /* Wait for APs to come up. We poll for ~1 second total. */
    for (int i = 0; i < 1000; i++) {
        delay_ms(1);
        bool all_started = true;
        for (int j = 1; j < MAX_CPUS; j++) {
            if (g_cpu_data[j].lapic_id != 0 && g_ap_started[j] == 0) {
                all_started = false;
                break;
            }
        }
        if (all_started) break;
    }

    int online = 0;
    for (int j = 1; j < MAX_CPUS; j++) {
        if (g_cpu_data[j].lapic_id == 0) continue;
        if (g_ap_started[j]) {
            online++;
            printk("SMP: AP %d (LAPIC %u) online\n",
                   j, (unsigned)g_cpu_data[j].lapic_id);
        } else {
            printk("SMP: AP %d (LAPIC %u) failed to come online (timeout)\n",
                   j, (unsigned)g_cpu_data[j].lapic_id);
            /* Phase 7: dump the trampoline diag region so we can
             * see exactly how far the AP got. Each byte of the
             * region was written by the AP at a specific milestone
             * (see ap_trampoline.S for the table). The BSP reads
             * the same physical bytes via the HHDM. The result
             * tells us which transition failed:
             *   - 0xF00 set, nothing else: AP entered 16-bit code
             *     but stalled before lgdt (e.g., bad GDTR patch)
             *   - 0xF04 set but 0xF05+: 16-bit code succeeded, the
             *     32-bit code is stuck
             *   - 0xF0A set but 0xF0D+: 64-bit code reached the
             *     entry point but the kernel GDT load (lgdt) or
             *     the retfq to load new CS failed
             *   - 0xF14 set: the AP called smp_ap_entry (reached C)
             *     but g_ap_started[j] is somehow not 1, which would
             *     be a bug in smp_ap_entry
             */
            phys_addr_t phys = ap_trampoline_phys[j];
            if (phys != 0) {
                const volatile uint8_t *diag = (const volatile uint8_t *)
                    (uintptr_t)(hhdm_request.response->offset + phys);
                /* Phase 7: clflush the diag region before reading so
                 * the BSP doesn't read a stale cached copy. x86 has
                 * strong ordering on stores to the same address, but
                 * clflush guarantees we'll read whatever the AP last
                 * wrote, even if the BSP's cache has a stale line. */
                for (uintptr_t p = (uintptr_t)diag; p < (uintptr_t)diag + 256; p += 64) {
                    __asm__ volatile ("clflush (%0)" : : "r"(p) : "memory");
                }
                __asm__ volatile ("mfence" ::: "memory");
                printk("SMP: AP %d diag:", j);
                for (int k = 0; k < 32; k++) {
                    uint8_t b = diag[0xF00 + k];
                    if (b != 0) {
                        printk(" [0x%02x]=0x%02x", 0xF00 + k, b);
                    }
                }
                /* Phase 7 canary: verify the BSP-side diag read works. */
                printk(" [bsp-canary@0xF80]=0x%x%s",
                       diag[0xF80],
                       diag[0xF80] == 0xAA ? " OK" : " MISSING");
                /* Phase 8: 16-bit code writes a fixed-address marker
                 * to 0x9F800 the first thing it runs. If the marker
                 * is set, the SIPI delivered to the 16-bit code; if
                 * not, the AP is still in the QEMU "wait for SIPI"
                 * code or stalled before the first instruction. */
                uint8_t fixed = *(volatile uint8_t *)0x9F800;
                printk(" [16bit-reached@0x9F800]=0x%x%s",
                       fixed, fixed == 0xA1 ? " OK" : " STALLED");
                printk("\n");
            }
        }
    }
    g_cpu_count = 1 + online;  /* BSP + online APs */
    printk("SMP: %llu CPU(s) online total\n", (unsigned long long)g_cpu_count);

    /* Phase 3d: self-test the IPI infrastructure by sending a
     * reschedule IPI to ourselves and verifying the handler
     * incremented the per-CPU counter. With only the BSP online,
     * the only way to exercise the LAPIC IPI path is self-IPI;
     * once APs come up, the same path will work cross-CPU. */
    {
        uint32_t before = g_ipi_count_reschedule;
        smp_send_ipi((uint32_t)g_cpu_data[0].lapic_id,
                     IPI_VECTOR_RESCHEDULE);
        /* The IPI is edge-triggered through the local APIC; the
         * handler runs at the next instruction boundary with
         * IRQs re-enabled. We spin briefly polling the counter
         * to absorb the latency without holding a lock. */
        for (int i = 0; i < 10000 && g_ipi_count_reschedule == before; i++) {
            __asm__ volatile ("pause");
        }
        if (g_ipi_count_reschedule == before) {
            printk("SMP: IPI self-test FAILED (reschedule counter did not advance)\n");
        } else {
            printk("SMP: IPI self-test passed (reschedule counter %u -> %u)\n",
                   (unsigned)before, (unsigned)g_ipi_count_reschedule);
        }
    }

    /* Phase 4: now that every AP is either up or has timed out,
     * unmap its trampoline page. The page is no longer reachable
     * (the AP is running kernel C with the kernel page tables;
     * the trampoline was only needed to get it there). Leaving
     * the mapping in place would be harmless but wastes a PT
     * entry in the kernel PML4 and exposes a 4 KiB hole in the
     * physical address space via the kernel half (since vmm_map
     * used the same phys for virt). */
    for (int j = 1; j < MAX_CPUS; j++) {
        if (g_cpu_data[j].lapic_id != 0) {
            cleanup_ap_trampoline(j);
        }
    }
}

void smp_init(void)
{
    printk("SMP: initializing\n");

    for (int i = 0; i < MAX_CPUS; i++) {
        g_cpu_data[i].self = (uint64_t)(uintptr_t)&g_cpu_data[i];
        g_cpu_data[i].cpu_id = i;
        g_cpu_data[i].lapic_id = 0;
        g_cpu_data[i].kernel_stack = 0;
        g_cpu_data[i].user_scratch = 0;
        g_cpu_data[i].current_task = 0;
        g_cpu_data[i].in_irq = 0;
        g_cpu_data[i].sched_ticks = 0;
        g_cpu_data[i].ap_should_halt = 0;
        /* Phase 4: per-CPU runqueue fields. The runqueue lock
         * lives in scheduler.c as a per-CPU array (see comment
         * in smp.h on why it's not in this struct). */
        g_cpu_data[i].runqueue_head = 0;
        g_cpu_data[i].runqueue_tail = 0;
        g_cpu_data[i].task_count = 0;
        g_cpu_data[i].idle_task = 0;
        /* Phase 6 followup: per-CPU reschedule flag, set by
         * ipi_dispatch when an IPI_VECTOR_RESCHEDULE arrives on
         * this CPU; consumed by the idle loop and by schedule(). */
        g_cpu_data[i].need_resched = 0;
        g_ap_started[i] = 0;
        ap_trampoline_phys[i] = 0;
    }

    if (!detect_apic()) {
        printk("SMP: CPU has no APIC (CPUID.1:EDX bit 9 = 0); SMP disabled\n");
        goto install_bsp_gs;
    }

    g_lapic_base = read_lapic_base_msr();
    if (g_lapic_base == 0) {
        printk("SMP: IA32_APIC_BASE returned 0; SMP disabled\n");
        goto install_bsp_gs;
    }

    smp_apic_supported = true;
    printk("SMP: LAPIC MMIO at phys 0x%llx\n", (unsigned long long)g_lapic_base);

    lapic_enable();

    struct cpu_data *bsp = &g_cpu_data[0];
    bsp->lapic_id = detect_lapic_id();
    bsp->kernel_stack = (uint64_t)(uintptr_t)&g_cpu_data[0];
    printk("SMP: BSP LAPIC ID = %u\n", (unsigned)bsp->lapic_id);

install_bsp_gs:
    wrmsr(MSR_GS_BASE, (uint64_t)(uintptr_t)&g_cpu_data[0]);
    wrmsr(MSR_KERNEL_GS_BASE, 0);

    /* Phase 8: map a single 4 KiB page at linear 0x9F000 in the kernel
     * PML4. The 16-bit trampoline code reads linear 0x9F000 to
     * discover the trampoline's own physical address (the BSP writes
     * the value to this mailbox just before sending each AP's SIPI).
     * We use a page-aligned address (0x9F000) because vmm_map rejects
     * non-page-aligned virtual addresses. 0x9F000 sits inside the
     * Limine USABLE region (0x69000..0x9EFFF); if Limine has already
     * identity-mapped it, vmm_map returns -1 and we use the existing
     * mapping. */
    {
        phys_addr_t mb_phys = pmm_alloc_page();
        if (mb_phys == 0) {
            printk("SMP: 0x9F000 mailbox alloc failed (no free page); AP bringup will fail\n");
        } else {
            int mr = vmm_map(vmm_kernel_address_space(), 0x9F000, mb_phys,
                              VMM_PRESENT | VMM_WRITABLE);
            if (mr != 0) {
                printk("SMP: 0x9F000 mailbox vmm_map rc=%d (limine probably mapped it); using existing mapping\n",
                       mr);
            } else {
                printk("SMP: 0x9F000 mailbox mapped at linear 0x9F000 (backed by phys 0x%llx)\n",
                       (unsigned long long)mb_phys);
            }
            /* Write a canary so we can verify the mapping from
             * the 16-bit code's perspective later. */
            *(volatile uint32_t *)0x9F000 = 0xCAFEBABE;
        }
    }

    /* Bring up the APs. This is a no-op if the CPU has no APIC. */
    smp_boot_aps();
}

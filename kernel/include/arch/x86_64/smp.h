/*
 * smp.h - Symmetric Multi-Processing support for LiteNix (Phase 1)
 *
 * This header defines the per-CPU data structure, LAPIC helpers, and the
 * AP startup interface. Phase 1 of the SMP work brings up APs to the
 * point where they have their own GDT/TSS, kernel stack, and cpu_data
 * pointer installed, and then enter a parked idle loop. Subsequent
 * phases (4+) will route scheduling and interrupts to them.
 */

#ifndef LITENIX_ARCH_X86_64_SMP_H
#define LITENIX_ARCH_X86_64_SMP_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

/* Maximum supported logical CPUs (APIC ID space is 8 bits = 256, but
 * 64 is more than enough for QEMU and a future laptop SoC). */
#define MAX_CPUS 64

/* Per-CPU storage accessed via %gs segment base.
 *
 * Offsets are kept stable so that the assembly syscall entry/exit stubs
 * and the AP trampoline can index fields without going through a struct
 * pointer. Field order:
 *
 *   offset 0   self          — pointer to this struct (for self-locating)
 *   offset 8   kernel_stack — top of kernel stack (used by syscall_entry.S)
 *   offset 16  user_scratch  — saved user RSP across syscall
 *   offset 24  cpu_id        — logical CPU index (0..cpu_count-1)
 *   offset 32  lapic_id      — LAPIC ID
 *   offset 40  current_task  — current_task pointer (per-CPU scheduler state)
 *   offset 48  in_irq        — nesting depth of IRQ handling on this CPU
 *   offset 56  sched_ticks   — local scheduler tick counter
 *   offset 64  ap_should_halt— 1 = enter halt loop; 0 = run normally
 *   offset 72  runqueue_head — per-CPU ready queue (Phase 4 SMP)
 *   offset 80  runqueue_tail
 *   offset 88  task_count    — number of tasks on this CPU's runqueue
 *   offset 96  idle_task     — per-CPU idle task pointer
 *   offset 104 need_resched  — per-CPU reschedule-pending flag
 *                              (set by reschedule IPI handler or by the
 *                              local timer tick; consumed by the idle loop)
 *
 * The struct must be kept tightly packed so the offsets above are stable.
 *
 * Phase 4 note: the per-CPU runqueue lock is *not* in this struct because
 * spinlock_t has its own alignment/state requirements that conflict with
 * the packed layout. The lock array lives next to the scheduler code
 * (`kernel/sched/scheduler.c`) and is indexed by cpu_id. */
struct cpu_data {
    uint64_t self;
    uint64_t kernel_stack;
    uint64_t user_scratch;
    uint64_t cpu_id;
    uint64_t lapic_id;
    uint64_t current_task;
    uint64_t in_irq;
    uint64_t sched_ticks;
    uint64_t ap_should_halt;
    uint64_t runqueue_head;
    uint64_t runqueue_tail;
    uint64_t task_count;
    uint64_t idle_task;
    uint64_t need_resched;
} __attribute__((packed));

/* Compile-time sanity: the offsets must match the documented values. */
_Static_assert(offsetof(struct cpu_data, self) == 0,       "cpu_data.self must be at offset 0");
_Static_assert(offsetof(struct cpu_data, kernel_stack) == 8,  "cpu_data.kernel_stack must be at offset 8");
_Static_assert(offsetof(struct cpu_data, user_scratch) == 16, "cpu_data.user_scratch must be at offset 16");
_Static_assert(offsetof(struct cpu_data, cpu_id) == 24,        "cpu_data.cpu_id must be at offset 24");
_Static_assert(offsetof(struct cpu_data, lapic_id) == 32,      "cpu_data.lapic_id must be at offset 32");
_Static_assert(offsetof(struct cpu_data, current_task) == 40,  "cpu_data.current_task must be at offset 40");
_Static_assert(offsetof(struct cpu_data, ap_should_halt) == 64, "cpu_data.ap_should_halt must be at offset 64");

/* Globals populated by smp_init(). */
extern uint64_t g_lapic_base;                              /* phys addr of LAPIC registers */
extern uint64_t g_cpu_count;                                /* number of CPUs brought up */
extern struct cpu_data g_cpu_data[MAX_CPUS];                /* indexed by logical cpu_id */
extern volatile uint32_t g_ap_started[MAX_CPUS];             /* 1 once AP has finished its setup */

/* IA32_APIC_BASE MSR (returns LAPIC base physical address in bits 35:12). */
#define MSR_IA32_APIC_BASE 0x1B

/* LAPIC register offsets. */
#define LAPIC_REG_ID            0x020
#define LAPIC_REG_VERSION       0x030
#define LAPIC_REG_TPR           0x080
#define LAPIC_REG_EOI           0x0B0
#define LAPIC_REG_SVR           0x0F0
#define LAPIC_REG_ICR_LOW       0x300
#define LAPIC_REG_ICR_HIGH      0x310
#define LAPIC_REG_LVT_TIMER     0x320
#define LAPIC_REG_LVT_THERMAL   0x330
#define LAPIC_REG_LVT_PMI       0x340
#define LAPIC_REG_LVT_LINT0     0x350
#define LAPIC_REG_LVT_LINT1     0x360
#define LAPIC_REG_LVT_ERROR     0x370
#define LAPIC_REG_TMR_ICR       0x380
#define LAPIC_REG_TMR_CCR       0x390
#define LAPIC_REG_TMR_DCR       0x3E0
#define LAPIC_REG_TMR_DIV       0x3F0
#define LAPIC_REG_SIVR          0x0F0   /* alias of SVR */

/* ICR delivery modes (placed in bits 10:8 of ICR low). */
#define ICR_FIXED               0x000
#define ICR_LOWEST_PRIORITY     0x100   /* delivery mode 1 (bit 8) */
#define ICR_SMI                 0x200   /* delivery mode 2 (bit 9) */
#define ICR_NMI                 0x400   /* delivery mode 4 (bit 10) */
#define ICR_INIT                0x500   /* delivery mode 5 (bits 8+10) */
#define ICR_STARTUP             0x600   /* delivery mode 6 (bits 9+10) */
#define ICR_ASSERT              (1u << 14)  /* 0 = deassert, 1 = assert */
#define ICR_DEST_FIELD          0x00000 /* shorthand: no shorthand (use ICR_HIGH) */
#define ICR_DEST_SELF           0x40000 /* shorthand: self (bit 18) */
#define ICR_DEST_ALL            0x80000 /* shorthand: all (incl. self) (bit 19) */
#define ICR_DEST_ALL_EXCL_SELF  0xC0000 /* shorthand: all excl. self (bits 18+19) */

/* IPIs the kernel defines for itself (vector 0x40+ is "IPI_VECTOR_BASE"). */
#define IPI_VECTOR_RESCHEDULE   0x40
#define IPI_VECTOR_TLB_SHOOTDOWN 0x41
#define IPI_VECTOR_PANIC        0x42
#define IPI_VECTOR_BASE         0x40

/* Accessors implemented in smp.c. */
uint32_t lapic_read(uint32_t reg);
void     lapic_write(uint32_t reg, uint32_t value);
bool     lapic_supported(void);

/* smp_init() — called from kernel_main() after scheduler init.
 * Detects APIC, brings up APs, installs per-CPU GS base. */
void smp_init(void);

/* Number of low-memory pages the SMP bring-up wants reserved up
 * front. pmm.c calls this very early so the trampoline pages
 * aren't handed out to the heap. */
int smp_max_trampoline_pages(void);

/* smp_ap_entry() — C entry point for APs. Called from ap_trampoline_64. */
void smp_ap_entry(void);

/* Helpers to query the current CPU. */
int                smp_current_cpu_id(void);
struct cpu_data    *smp_current_cpu(void);
struct cpu_data    *smp_cpu_data(int cpu_id);

/* Per-CPU `current_task` accessors. Phase 3c keeps a global
 * `current_task` (BSP's view) and shadows it in cpu_data; APs will
 * eventually write to their own slot. The accessor pair is the
 * canonical read/write path for any code that needs to know what
 * this CPU is currently running. */
struct task;
struct task *smp_get_current_task(void);
void         smp_set_current_task(struct task *t);

/* IPI send helpers. */
void smp_send_ipi(uint32_t lapic_id, uint32_t vector);
void smp_broadcast_ipi_excluding_self(uint32_t vector);
void smp_send_ipi_to_cpu(int cpu_id, uint32_t vector);

/* Phase 32 followup #3: send an IPI to the calling CPU. Skips
 * the skip-self short-circuit of smp_send_ipi_to_cpu. Used by
 * self-tests to exercise the IPI receive path on the BSP without
 * needing live APs. */
void smp_self_ipi(uint32_t vector);

/* Called by the BSP to halt a target AP during teardown (no-op in Phase 1). */
void smp_halt_cpu(int cpu_id);

/* Phase 32 (followup): tell every other CPU to halt. Called from
 * panic() so a panicking CPU broadcasts a halt signal to its peers
 * before itself entering the cli/hlt loop. The IPI handler on the
 * receiving side halts via cpu_halt(). */
void smp_broadcast_panic(void);

/* Phase 3d: per-IPI-vector delivery counters (read by the BSP for
 * diagnostics, written by the IPI handler with IRQs disabled). */
extern volatile uint32_t g_ipi_count_reschedule;
extern volatile uint32_t g_ipi_count_tlb_shootdown;
extern volatile uint32_t g_ipi_count_panic;

/* Phase 3d: IPI dispatch. Called from isr_ipi_common (in isr.S)
 * with the saved register state on the stack; decodes the vector
 * and dispatches to the right handler. */
#include <arch/x86_64/interrupt.h>
void ipi_dispatch(struct interrupt_frame *frame);

/* Phase 5: per-CPU TLB-shootdown queues. vmm_unmap() publishes the
 * virtual address to every other CPU's queue (one entry per unmap),
 * then broadcasts IPI_VECTOR_TLB_SHOOTDOWN. Each receiver drains its
 * own queue in the IPI handler and does invlpg for each address. The
 * LAPIC IPI write is a serializing event on x86, so the receiver
 * sees the entry writes the sender performed before the IPI.
 *
 * The queue is bounded (TLB_QUEUE_PER_CPU entries); if it overflows
 * the new entry overwrites the oldest unprocessed one. With
 * TLB_QUEUE_PER_CPU=16 and a per-CPU unmap rate well below that, this
 * never matters in practice. If it ever does, the right fix is to
 * send a second IPI to drain the rest; for now we accept the loss.
 *
 * Aligned to 64 to keep the producer head and consumer tail on
 * separate cache lines (avoiding false sharing between the sender
 * writing to one CPU's queue and that CPU reading it). */
#define TLB_QUEUE_PER_CPU 16
struct tlb_shootdown_entry {
    uint64_t vaddr;
};
struct tlb_shootdown_queue {
    volatile uint32_t head;       /* producer writes */
    volatile uint32_t tail;       /* consumer reads */
    struct tlb_shootdown_entry entries[TLB_QUEUE_PER_CPU];
} __attribute__((aligned(64)));

extern struct tlb_shootdown_queue g_tlb_queue[MAX_CPUS];

/* Publish a single vaddr to every other CPU's shootdown queue.
 * The caller is responsible for sending the IPI after this returns. */
void tlb_shootdown_publish_all_others(uint64_t virt);

/* Drain the current CPU's shootdown queue, doing invlpg for each
 * pending entry. Called from ipi_dispatch when the IPI vector is
 * IPI_VECTOR_TLB_SHOOTDOWN. */
void tlb_shootdown_drain(void);

/* Phase 5 self-test: enqueues entries into the BSP's own queue and
 * drains them, verifying head/tail accounting and the invlpg path
 * without needing live APs. Runs at kernel boot. */
void tlb_shootdown_self_test(void);

/* Phase 6 self-test: fakes a second CPU with a non-existent LAPIC
 * id and calls smp_send_ipi_to_cpu(), verifying the bounded wait
 * in lapic_send_ipi() returns instead of hanging on a missing
 * target. */
void reschedule_self_test(void);

/* Phase 32 followup self-test: fakes a second CPU and calls
 * smp_broadcast_panic(), verifying the broadcast path doesn't
 * hang the BSP on a missing target. */
void panic_broadcast_self_test(void);

/* Phase 32 followup self-test: invoke smp_handle_ipi() for the
 * reschedule and TLB-shootdown paths and verify the handlers set
 * the right state (per-CPU need_resched flag, per-CPU TLB queue
 * drained, delivery counters bumped). The panic path is not
 * tested (it would halt). */
void ipi_handler_self_test(void);

#endif /* LITENIX_ARCH_X86_64_SMP_H */

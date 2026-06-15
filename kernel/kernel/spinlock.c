/*
 * spinlock.c - test-and-set spinlocks (Phase 3a).
 *
 * The implementation is the textbook TATAS spinlock:
 *   - `xchg` to acquire the lock atomically (with a `lock` prefix so
 *     the read-modify-write is atomic on the bus).
 *   - A bounded PAUSE loop on failure to avoid hammering the memory
 *     controller; if the loop overflows, we do an `sti; hlt` to yield
 *     the CPU to a sibling. With only the BSP running today, the
 *     loop bound is mostly cosmetic, but it will matter once Phase 2
 *     actually brings APs online.
 *
 * The IRQ-disabling variant uses the same `save_interrupts_and_disable`
 * helper from arch/x86_64/cpu.h that the rest of the kernel uses for
 * critical sections. The IRQs are re-enabled by the matching restore.
 */

#include <kernel/spinlock.h>
#include <arch/x86_64/cpu.h>
#include <kernel/printk.h>
#include <stdint.h>
#include <stddef.h>

#define SPINLOCK_MAX_SPINS   1000000
#define SPINLOCK_YIELD_SPINS 1000

/* Read the current value of the IF (interrupt flag) bit in RFLAGS.
 * Used by the yield path to decide whether we can safely re-enable
 * interrupts and halt to give a sibling CPU a chance to make progress. */
static inline bool interrupts_currently_enabled(void)
{
    uint64_t rflags;
    __asm__ volatile (
        "pushfq\n"
        "pop %0\n"
        : "=r"(rflags)
        :
        : "memory");
    return (rflags & (1ULL << 9)) != 0;
}

void spinlock_init(spinlock_t *lock, const char *name)
{
    lock->locked    = 0;
    lock->owner_cpu = (uint32_t)-1;
    lock->name      = name;
}

static inline uint32_t xchg32(volatile uint32_t *p, uint32_t v)
{
    uint32_t prev;
    __asm__ volatile (
        "xchgl %0, %1"
        : "=r"(prev), "+m"(*p)
        : "0"(v)
        : "memory");
    return prev;
}

static inline uint32_t cmpxchg32(volatile uint32_t *p, uint32_t expected, uint32_t desired)
    __attribute__((unused));
static inline uint32_t cmpxchg32(volatile uint32_t *p, uint32_t expected, uint32_t desired)
{
    uint32_t prev;
    __asm__ volatile (
        "lock cmpxchgl %2, %1"
        : "=a"(prev), "+m"(*p)
        : "r"(desired), "0"(expected)
        : "memory", "cc");
    return prev;
}

static inline uint32_t current_cpu_id_unsafe(void)
{
    /* Read MSR_GS_BASE in two halves. The "=A" constraint only
     * captures EAX (low 32 bits); for a 64-bit MSR we need to
     * also pick up EDX. A bug here would read the user-side GS
     * base on a user-mode exception, or — worse — a kernel-half
     * address with the high 32 bits truncated to 0, leading to a
     * bogus low-half pointer and a spurious page fault. */
    uint32_t lo, hi;
    __asm__ volatile ("rdmsr" : "=a"(lo), "=d"(hi) : "c"((uint32_t)0xC0000101) : "memory");
    uint64_t gs = ((uint64_t)hi << 32) | lo;
    if (gs == 0) return 0;
    uint32_t id;
    __asm__ volatile ("mov 24(%1), %0" : "=r"(id) : "r"(gs) : "memory");
    return id;
}

void spin_lock(spinlock_t *lock)
{
    uint32_t spins = 0;
    for (;;) {
        uint32_t prev = xchg32(&lock->locked, 1);
        if (prev == 0) {
            /* Acquired. */
            lock->owner_cpu = current_cpu_id_unsafe();
            return;
        }
        if (++spins > SPINLOCK_MAX_SPINS) {
            printk("SPINLOCK: possible deadlock on %s (held by CPU %u)\n",
                   lock->name ? lock->name : "?",
                   lock->owner_cpu);
            spins = 0;
        }
        for (uint32_t i = 0; i < SPINLOCK_YIELD_SPINS; i++) {
            __asm__ volatile ("pause");
        }
        /* If interrupts are enabled we may as well yield, so a sibling
         * can finish its critical section. With only the BSP today
         * this is a no-op, but it makes the lock well-behaved once
         * APs are online. */
        if (interrupts_currently_enabled()) {
            __asm__ volatile ("sti; hlt; cli");
        }
    }
}

void spin_unlock(spinlock_t *lock)
{
    /* xchg with 0 acts as a full memory barrier on x86, so the data
     * the holder wrote is visible to the next acquirer. */
    xchg32(&lock->locked, 0);
    lock->owner_cpu = (uint32_t)-1;
}

void spin_lock_irqsave(spinlock_t *lock, uint64_t *flags)
{
    *flags = save_interrupts_and_disable();
    spin_lock(lock);
}

void spin_unlock_irqrestore(spinlock_t *lock, uint64_t flags)
{
    spin_unlock(lock);
    restore_interrupts((bool)flags);
}

bool spin_trylock(spinlock_t *lock)
{
    uint32_t prev = xchg32(&lock->locked, 1);
    if (prev == 0) {
        lock->owner_cpu = current_cpu_id_unsafe();
        return true;
    }
    return false;
}

/* Self-test for the spinlock primitives. We exercise acquire/release
 * and the IRQ-disabling variant, and we verify the locked counter.
 * Without contention the test is mostly a smoke test, but it
 * catches obvious bugs in the inline asm and the unlock barrier. */
void spinlock_self_test(void)
{
    spinlock_t l = SPINLOCK_INIT("selftest");
    uint64_t flags = 0;

    spin_lock(&l);
    if (l.locked != 1) {
        printk("SPINLOCK: self-test failed: locked=%u after acquire\n", l.locked);
        return;
    }
    spin_unlock(&l);
    if (l.locked != 0) {
        printk("SPINLOCK: self-test failed: locked=%u after release\n", l.locked);
        return;
    }

    spin_lock_irqsave(&l, &flags);
    if (l.locked != 1) {
        printk("SPINLOCK: self-test failed: locked=%u after irqsave acquire\n", l.locked);
        return;
    }
    spin_unlock_irqrestore(&l, flags);
    if (l.locked != 0) {
        printk("SPINLOCK: self-test failed: locked=%u after irqrestore release\n", l.locked);
        return;
    }

    if (!spin_trylock(&l)) {
        printk("SPINLOCK: self-test failed: trylock on free lock returned false\n");
        return;
    }
    if (spin_trylock(&l)) {
        printk("SPINLOCK: self-test failed: trylock on held lock returned true\n");
        return;
    }
    spin_unlock(&l);

    printk("SPINLOCK: self-test passed\n");
}

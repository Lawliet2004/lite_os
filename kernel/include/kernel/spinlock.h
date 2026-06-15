/*
 * spinlock.h - test-and-set spinlocks with optional IRQ disabling.
 *
 * Phase 3 of the SMP work introduces per-CPU data structures that
 * need mutual exclusion across CPUs (the runqueue, the VFS inode
 * cache, the heap's freelist, etc.). The kernel has not yet wired
 * up any IPI-based queueing, so we use the simple "spin until the
 * lock is free" approach with a bounded PAUSE-loop count and a
 * fallback yield (an `sti; hlt` pause) so we don't burn a core.
 *
 * Locking rules
 * -------------
 *   - `spin_lock(&l)`  / `spin_unlock(&l)`           : plain spinlock
 *   - `spin_lock_irqsave(&l, flags)` / `..._restore`: spinlock that
 *     also disables IRQs on the local CPU. Use this when the lock
 *     protects a data structure that an IRQ handler might also touch.
 *   - Always pair `_irqsave` with `_irqrestore` to keep the IRQ state
 *     balanced.
 *
 * SMP ordering
 * ------------
 *   x86 has a strong total-store-order (TSO) memory model; the
 *   `xchg` / `lock` prefix on every acquisition + release acts as a
 *   full memory barrier, so we do not need explicit `mfence`.
 */

#ifndef LITENIX_KERNEL_SPINLOCK_H
#define LITENIX_KERNEL_SPINLOCK_H

#include <stdint.h>
#include <stdbool.h>

typedef struct {
    volatile uint32_t locked;   /* 0 = free, 1 = held */
    uint32_t          owner_cpu; /* cpu_id of the holder, for diagnostics */
    const char       *name;      /* optional, for deadlock dumps */
} spinlock_t;

#define SPINLOCK_INIT(n) { .locked = 0, .owner_cpu = (uint32_t)-1, .name = (n) }

void spinlock_init(spinlock_t *lock, const char *name);

/* Acquire the lock with IRQs still enabled. */
void spin_lock(spinlock_t *lock);

/* Release the lock; the caller must not have IRQs disabled. */
void spin_unlock(spinlock_t *lock);

/* Acquire the lock and disable local IRQs. `flags` receives the
 * previous IRQ state so it can be passed back to spin_unlock_irqrestore. */
void spin_lock_irqsave(spinlock_t *lock, uint64_t *flags);

/* Release the lock and restore the previous IRQ state. */
void spin_unlock_irqrestore(spinlock_t *lock, uint64_t flags);

/* Try to acquire; return true if successful. Does not spin. */
bool spin_trylock(spinlock_t *lock);

/* Exercise acquire/release and the IRQ-disabling variant. Safe to
 * call on the BSP (no other CPU holds the lock); single-CPU today,
 * with a real contention test to come in Phase 4. */
void spinlock_self_test(void);

/* Compile-time check that the lock fits in a cache line and is
 * properly aligned. The struct above is small (16 bytes), so this
 * is more documentation than enforcement. */
_Static_assert(sizeof(spinlock_t) <= 64, "spinlock_t must fit in a cache line");

#endif

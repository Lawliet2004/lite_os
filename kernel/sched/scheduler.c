#include <sched/scheduler.h>
#include <sched/task.h>
#include <arch/x86_64/gdt.h>
#include <arch/x86_64/vmm.h>
#include <arch/x86_64/cpu.h>
#include <arch/x86_64/syscall_entry.h>
#include <arch/x86_64/smp.h>
#include <drivers/pit.h>
#include <kernel/panic.h>
#include <kernel/printk.h>
#include <kernel/spinlock.h>
#include <lib/string.h>

extern void context_switch(uint64_t *old_rsp, uint64_t new_rsp);

/* Phase 4: the runqueue stays global for now. The per-CPU fields
 * in struct cpu_data (runqueue_head/tail/task_count/idle_task) exist
 * and are zeroed by smp_init, but the scheduler continues to use
 * these globals. The migration to per-CPU was attempted twice and
 * showed addc=3, add=3 in the BSP-only test path (all tasks
 * correctly added to the per-CPU runqueue), but the BSP then
 * failed to make progress through the Phase 9 & 10 test suite —
 * the boot hung at the test_read_kernel page fault regardless of
 * whether the scheduler used spinlock or cli/sti. The bisect
 * couldn't isolate the hang in the BSP-only path; needs live APs
 * to reproduce. Reverted to globals so the kernel boots. */
static struct task *runqueue_head;
static struct task *runqueue_tail;
static uint64_t task_count;

/* Phase 6 followup: need_resched is per-CPU. The IPI handler on
 * the target CPU sets g_cpu_data[target].need_resched = 1; the
 * target's idle loop / next schedule() consumes it. The local
 * timer tick path also uses the per-CPU flag (Phase 6 hookup
 * in sched_tick). The BSP's per-CPU flag is g_cpu_data[0].need_resched
 * (the old static global is gone). */

static spinlock_t runqueue_lock = SPINLOCK_INIT("sched.runqueue");

static struct task *idle_task;
static struct task *sleep_list_head;
static uint64_t last_boost_tick;

void sched_init(void)
{
    runqueue_head = 0;
    runqueue_tail = 0;
    task_count = 0;
    /* The per-CPU need_resched is zeroed in smp_init (smp.c).
     * We additionally clear it on the BSP here in case smp_init
     * ran before sched_init (it doesn't, but defensive). */
    if (smp_current_cpu_id() == 0) {
        g_cpu_data[0].need_resched = 0;
    }
    idle_task = 0;
    sleep_list_head = 0;
    last_boost_tick = 0;
}

void sched_set_idle_task(struct task *task)
{
    idle_task = task;
}

void sched_add_task(struct task *task)
{
    if (task == 0) return;

    spin_lock(&runqueue_lock);

    struct task *cursor = runqueue_head;
    while (cursor != 0) {
        if (cursor == task) {
            spin_unlock(&runqueue_lock);
            return;
        }
        cursor = cursor->next;
    }

    task->next = 0;
    if (runqueue_tail != 0) {
        runqueue_tail->next = task;
    } else {
        runqueue_head = task;
    }
    runqueue_tail = task;
    task_count++;

    spin_unlock(&runqueue_lock);
}

void sched_remove_task(struct task *task)
{
    if (task == 0 || task == current_task) return;

    spin_lock(&runqueue_lock);

    struct task *prev = 0;
    struct task *cursor = runqueue_head;
    while (cursor != 0) {
        if (cursor == task) {
            if (prev != 0) {
                prev->next = cursor->next;
            } else {
                runqueue_head = cursor->next;
            }
            if (runqueue_tail == cursor) {
                runqueue_tail = prev;
            }
            cursor->next = 0;
            if (task_count > 0) task_count--;
            spin_unlock(&runqueue_lock);
            return;
        }
        prev = cursor;
        cursor = cursor->next;
    }

    spin_unlock(&runqueue_lock);
}

void sched_add_sleeper(struct task *task)
{
    if (task == 0) return;

    struct task *prev = 0;
    struct task *cursor = sleep_list_head;
    while (cursor != 0 && cursor->sleep_until <= task->sleep_until) {
        prev = cursor;
        cursor = cursor->sleep_next;
    }
    task->sleep_next = cursor;
    if (prev != 0) {
        prev->sleep_next = task;
    } else {
        sleep_list_head = task;
    }
}

void sched_remove_sleeper(struct task *task)
{
    if (task == 0) return;

    struct task *prev = 0;
    struct task *cursor = sleep_list_head;
    while (cursor != 0) {
        if (cursor == task) {
            if (prev != 0) {
                prev->sleep_next = cursor->sleep_next;
            } else {
                sleep_list_head = cursor->sleep_next;
            }
            return;
        }
        prev = cursor;
        cursor = cursor->sleep_next;
    }
}

void sched_check_sleepers(void)
{
    uint64_t now = pit_ticks();

    while (sleep_list_head != 0 && sleep_list_head->sleep_until <= now) {
        struct task *task = sleep_list_head;
        sleep_list_head = task->sleep_next;
        task->sleep_next = 0;
        task->sleep_until = 0;
        task->sleep_timed_out = true;

        if (task->state == TASK_SLEEPING) {
            task->state = TASK_READY;
            task->time_slice = task->time_slice_max;
        }
    }
}

void sched_boost_starved(void)
{
    uint64_t now = pit_ticks();
    struct task *cursor = runqueue_head;
    while (cursor != 0) {
        if (cursor->state == TASK_READY
            && (cursor->flags & TASK_FLAG_IDLE) == 0
            && cursor->priority > SCHED_PRIO_MIN) {
            uint64_t idle_ticks = now - cursor->last_run_tick;
            if (idle_ticks >= SCHED_STARVE_THRESHOLD) {
                cursor->priority--;
                cursor->time_slice_max = sched_prio_to_slice(cursor->priority);
            }
        }
        cursor = cursor->next;
    }
}

void sched_tick(void)
{
    if (current_task != 0) {
        current_task->total_ticks++;

        if (current_task->time_slice > 0) {
            current_task->time_slice--;
        }

        if (current_task->time_slice == 0) {
            current_task->time_slice = current_task->time_slice_max;
            if (current_task->priority != current_task->static_priority
                && (current_task->flags & TASK_FLAG_IDLE) == 0) {
                current_task->priority = current_task->static_priority;
                current_task->time_slice_max =
                    sched_prio_to_slice(current_task->priority);
                current_task->time_slice = current_task->time_slice_max;
            }
            {
                int rcpu = smp_current_cpu_id();
                if (rcpu >= 0 && rcpu < MAX_CPUS) {
                    g_cpu_data[rcpu].need_resched = 1;
                }
            }
            if (g_cpu_count > 1) {
                smp_send_ipi_to_cpu(smp_current_cpu_id(), IPI_VECTOR_RESCHEDULE);
            }
        }
    }

    sched_check_sleepers();

    uint64_t now = pit_ticks();
    if (now - last_boost_tick >= SCHED_BOOST_INTERVAL) {
        last_boost_tick = now;
        sched_boost_starved();
    }
}

bool sched_needs_resched(void)
{
    int rcpu = smp_current_cpu_id();
    if (rcpu < 0 || rcpu >= MAX_CPUS) return false;
    return g_cpu_data[rcpu].need_resched != 0;
}

static struct task *pick_next_task(struct task *prev)
{
    if (runqueue_head == 0) return idle_task;

    struct task *best = 0;
    int8_t best_prio = SCHED_PRIO_MAX + 1;
    bool past_prev = false;
    bool best_is_past_prev = false;

    struct task *cursor = runqueue_head;
    while (cursor != 0) {
        if (cursor == prev) {
            past_prev = true;
            cursor = cursor->next;
            continue;
        }

        if (cursor->state == TASK_READY
            && (cursor->flags & TASK_FLAG_IDLE) == 0) {
            if (cursor->priority < best_prio) {
                best_prio = cursor->priority;
                best = cursor;
                best_is_past_prev = past_prev;
            } else if (cursor->priority == best_prio) {
                if (past_prev && !best_is_past_prev) {
                    best = cursor;
                    best_is_past_prev = true;
                }
            }
        }

        cursor = cursor->next;
    }

    if (best != 0) return best;

    if (idle_task != 0 && idle_task->state != TASK_ZOMBIE) {
        return idle_task;
    }

    return 0;
}

void schedule(void)
{
    int rcpu = smp_current_cpu_id();
    if (rcpu >= 0 && rcpu < MAX_CPUS) {
        g_cpu_data[rcpu].need_resched = 0;
    }

    struct task *prev = current_task;
    if (prev == 0) return;
    if (runqueue_head == 0) return;

    struct task *next = pick_next_task(prev);
    if (next == 0 || next == prev) return;

    if (prev->state == TASK_RUNNING) {
        prev->state = TASK_READY;
    }

    next->state = TASK_RUNNING;
    next->last_run_tick = pit_ticks();
    if (next->priority != next->static_priority
        && (next->flags & TASK_FLAG_IDLE) == 0) {
        next->priority = next->static_priority;
        next->time_slice_max = sched_prio_to_slice(next->priority);
    }

    struct task *old = current_task;
    current_task = next;
    smp_set_current_task(next);

    if (old != next) {
        tss_set_rsp0((uint64_t)next->kstack_top);
        syscall_set_kernel_rsp((uint64_t)next->kstack_top);
        if (old->cr3 != next->cr3) {
            vmm_switch_address_space(next->process->address_space);
        }
        if (old->fs_base != next->fs_base) {
            write_fs_base(next->fs_base);
        }
        context_switch(&old->saved_rsp, next->saved_rsp);
    }
}

uint64_t sched_task_count(void)
{
    return task_count;
}

struct task *sched_get_runqueue_head(void)
{
    return runqueue_head;
}

void sched_dump(const char *label)
{
    printk("Sched[%s]: count=%llu runqueue=[", label, task_count);
    struct task *cursor = runqueue_head;
    bool first = true;
    while (cursor != 0) {
        if (!first) printk(",");
        printk("%s/%llu/p%d/s%u/%s", cursor->name, (unsigned long long)cursor->tid,
            cursor->priority, cursor->time_slice,
            cursor->state == TASK_READY ? "ready"
            : cursor->state == TASK_RUNNING ? "running"
            : cursor->state == TASK_SLEEPING ? "sleep"
            : "zombie");
        first = false;
        cursor = cursor->next;
    }
    printk("]\n");

    if (sleep_list_head != 0) {
        printk("Sched[%s]: sleepers=[", label);
        cursor = sleep_list_head;
        first = true;
        while (cursor != 0) {
            if (!first) printk(",");
            printk("%s/%llu@%llu", cursor->name,
                (unsigned long long)cursor->tid,
                (unsigned long long)cursor->sleep_until);
            first = false;
            cursor = cursor->sleep_next;
        }
        printk("]\n");
    }
}

void sched_self_test(void)
{
    printk("Sched: initialized with %llu tasks (Phase 7)\n", task_count);
}

void sched_exit_group(struct process *proc, int exit_code)
{
    if (proc == 0) return;

    bool was_enabled = save_interrupts_and_disable();

    struct task *prev_task = 0;
    struct task *curr = runqueue_head;
    while (curr != 0) {
        struct task *next = curr->next;
        if (curr->process == proc && curr != current_task) {
            curr->state = TASK_ZOMBIE;
            curr->exit_code = exit_code;

            if (prev_task != 0) {
                prev_task->next = next;
            } else {
                runqueue_head = next;
            }
            if (runqueue_tail == curr) {
                runqueue_tail = prev_task;
            }
            task_count--;
        } else {
            prev_task = curr;
        }
        curr = next;
    }

    prev_task = 0;
    curr = sleep_list_head;
    while (curr != 0) {
        struct task *next = curr->sleep_next;
        if (curr->process == proc && curr != current_task) {
            curr->state = TASK_ZOMBIE;
            curr->exit_code = exit_code;

            if (prev_task != 0) {
                prev_task->sleep_next = next;
            } else {
                sleep_list_head = next;
            }
        } else {
            prev_task = curr;
        }
        curr = next;
    }

    restore_interrupts(was_enabled);
}

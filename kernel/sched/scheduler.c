#include <sched/scheduler.h>
#include <sched/task.h>
#include <arch/x86_64/gdt.h>
#include <arch/x86_64/vmm.h>
#include <arch/x86_64/cpu.h>
#include <arch/x86_64/syscall_entry.h>
#include <drivers/pit.h>
#include <kernel/panic.h>
#include <kernel/printk.h>
#include <lib/string.h>

extern void context_switch(uint64_t *old_rsp, uint64_t new_rsp);

static struct task *runqueue_head;
static struct task *runqueue_tail;
static uint64_t task_count;
static volatile bool need_resched;

/* Phase 7: idle task pointer (fallback when nothing else is READY) */
static struct task *idle_task;

/* Phase 7: sleep list for timed sleepers */
static struct task *sleep_list_head;

/* Phase 7: starvation boost tracking */
static uint64_t last_boost_tick;

void sched_init(void)
{
    runqueue_head = 0;
    runqueue_tail = 0;
    task_count = 0;
    need_resched = false;
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

    /* Check for duplicates */
    struct task *cursor = runqueue_head;
    while (cursor != 0) {
        if (cursor == task) {
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
}

void sched_remove_task(struct task *task)
{
    if (task == 0 || task == current_task) return;

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
            return;
        }
        prev = cursor;
        cursor = cursor->next;
    }
}

/* Phase 7: add a task to the sleep list (timed sleep) */
void sched_add_sleeper(struct task *task)
{
    if (task == 0) return;

    /* Insert sorted by sleep_until for efficient wakeup scanning */
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

/* Phase 7: remove a task from the sleep list */
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
            cursor->sleep_next = 0;
            return;
        }
        prev = cursor;
        cursor = cursor->sleep_next;
    }
}

/*
 * Phase 7: check timed sleepers and wake any whose deadline has passed.
 * Called from sched_tick() with interrupts already disabled.
 */
void sched_check_sleepers(void)
{
    uint64_t now = pit_ticks();

    while (sleep_list_head != 0 && sleep_list_head->sleep_until <= now) {
        struct task *task = sleep_list_head;
        sleep_list_head = task->sleep_next;
        task->sleep_next = 0;
        task->sleep_until = 0;

        if (task->state == TASK_SLEEPING) {
            task->state = TASK_READY;
            task->time_slice = task->time_slice_max;
            /* Task is still in the runqueue; pick_next_task will find it */
        }
    }
}

/*
 * Phase 7: starvation prevention.
 * Only boost tasks that haven't run for at least SCHED_STARVE_THRESHOLD
 * PIT ticks. We restore a task's static_priority when it actually runs
 * (in schedule()), so accumulated boost never exceeds -1 per interval
 * for a task that keeps getting CPU.
 */
void sched_boost_starved(void)
{
    uint64_t now = pit_ticks();
    struct task *cursor = runqueue_head;
    while (cursor != 0) {
        if (cursor->state == TASK_READY
            && (cursor->flags & TASK_FLAG_IDLE) == 0
            && cursor->priority > SCHED_PRIO_MIN) {
            /* Only boost if task hasn't run recently */
            uint64_t idle_ticks = now - cursor->last_run_tick;
            if (idle_ticks >= SCHED_STARVE_THRESHOLD) {
                cursor->priority--;
                cursor->time_slice_max = sched_prio_to_slice(cursor->priority);
            }
        }
        cursor = cursor->next;
    }
}

/*
 * Phase 7: sched_tick — called from PIT IRQ handler.
 *
 * 1. Account CPU time to current task
 * 2. Decrement time-slice; if expired, request reschedule
 * 3. Check timed sleepers for wakeup
 * 4. Periodically boost starved tasks
 */
void sched_tick(void)
{
    if (current_task != 0) {
        current_task->total_ticks++;

        /* Decrement time-slice; idle task always yields */
        if (current_task->time_slice > 0) {
            current_task->time_slice--;
        }

        if (current_task->time_slice == 0) {
            /* Reload slice for next run */
            current_task->time_slice = current_task->time_slice_max;
            /* Restore boosted priority after running */
            if (current_task->priority != current_task->static_priority
                && (current_task->flags & TASK_FLAG_IDLE) == 0) {
                current_task->priority = current_task->static_priority;
                current_task->time_slice_max =
                    sched_prio_to_slice(current_task->priority);
                current_task->time_slice = current_task->time_slice_max;
            }
            need_resched = true;
        }
    }

    /* Wake timed sleepers */
    sched_check_sleepers();

    /* Starvation boost scan */
    uint64_t now = pit_ticks();
    if (now - last_boost_tick >= SCHED_BOOST_INTERVAL) {
        last_boost_tick = now;
        sched_boost_starved();
    }
}

bool sched_needs_resched(void)
{
    return need_resched;
}

/*
 * Phase 7: pick_next_task — priority-aware selection.
 *
 * Scan the runqueue for the READY task with the lowest priority value
 * (= highest scheduling priority). Among equal priorities, prefer the
 * first one found after `prev` for round-robin fairness within the same
 * priority level.
 *
 * The idle task is returned only if no other READY task exists.
 */
static struct task *pick_next_task(struct task *prev)
{
    if (runqueue_head == 0) return idle_task;

    struct task *best = 0;
    int8_t best_prio = SCHED_PRIO_MAX + 1;
    bool past_prev = false;
    bool best_is_past_prev = false;

    /*
     * Two-pass scan: first scan from after prev to end, then from head to prev.
     * This ensures round-robin among equal-priority tasks.
     */
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

    /* If we found a non-idle READY task, use it */
    if (best != 0) return best;

    /* Fall back to idle task */
    if (idle_task != 0 && idle_task->state != TASK_ZOMBIE) {
        return idle_task;
    }

    return 0;
}

void schedule(void)
{
    need_resched = false;

    struct task *prev = current_task;
    if (prev == 0) return;
    if (runqueue_head == 0) return;

    struct task *next = pick_next_task(prev);
    if (next == 0 || next == prev) return;

    if (prev->state == TASK_RUNNING) {
        prev->state = TASK_READY;
    }

    next->state = TASK_RUNNING;
    next->last_run_tick = pit_ticks(); /* record when task last ran */
    /* Restore static priority after it has run (clear starvation boost) */
    if (next->priority != next->static_priority
        && (next->flags & TASK_FLAG_IDLE) == 0) {
        next->priority = next->static_priority;
        next->time_slice_max = sched_prio_to_slice(next->priority);
    }

    struct task *old = current_task;
    current_task = next;

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

    /* Dump sleep list */
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

    /* Mark all other tasks of this process as ZOMBIE and remove them from scheduler */
    struct task *prev_task = 0;
    struct task *curr = runqueue_head;
    while (curr != 0) {
        struct task *next = curr->next;
        if (curr->process == proc && curr != current_task) {
            curr->state = TASK_ZOMBIE;
            curr->exit_code = exit_code;

            // Remove from runqueue
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

    // Do the same for timed sleepers in sleep_list
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

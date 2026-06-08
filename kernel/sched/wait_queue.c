#include <arch/x86_64/cpu.h>
#include <kernel/printk.h>
#include <lib/string.h>
#include <sched/scheduler.h>
#include <sched/wait_queue.h>

void wait_queue_init(struct wait_queue *wq)
{
    if (wq == 0) return;
    wq->head = 0;
    wq->tail = 0;
}

void wait_queue_sleep_locked(struct wait_queue *wq)
{
    if (wq == 0 || current_task == 0) return;

    struct task *self = current_task;

    if (wq->tail != 0) {
        wq->tail->wait_next = self;
    } else {
        wq->head = self;
    }
    wq->tail = self;
    self->wait_next = 0;
    self->state = TASK_SLEEPING;
    self->wait_queue = wq;

    schedule();

    self->wait_queue = 0;
    self->wait_next = 0;
}

void wait_queue_sleep(struct wait_queue *wq)
{
    if (wq == 0 || current_task == 0) return;

    bool was_enabled = save_interrupts_and_disable();

    wait_queue_sleep_locked(wq);

    restore_interrupts(was_enabled);
}

void wait_queue_wake_one(struct wait_queue *wq)
{
    if (wq == 0) return;

    bool was_enabled = save_interrupts_and_disable();

    struct task *task = wq->head;
    if (task != 0) {
        wq->head = task->wait_next;
        if (wq->head == 0) {
            wq->tail = 0;
        }
        task->wait_next = 0;

        if (task->state == TASK_SLEEPING) {
            task->state = TASK_READY;
            sched_add_task(task);
        }
    }

    restore_interrupts(was_enabled);
}

void wait_queue_wake_all(struct wait_queue *wq)
{
    if (wq == 0) return;

    bool was_enabled = save_interrupts_and_disable();

    struct task *task = wq->head;
    while (task != 0) {
        struct task *next = task->wait_next;
        task->wait_next = 0;
        if (task->state == TASK_SLEEPING) {
            task->state = TASK_READY;
            sched_add_task(task);
        }
        task = next;
    }
    wq->head = 0;
    wq->tail = 0;

    restore_interrupts(was_enabled);
}

uint64_t wait_queue_count(const struct wait_queue *wq)
{
    if (wq == 0) return 0;

    uint64_t count = 0;
    for (struct task *cursor = wq->head; cursor != 0; cursor = cursor->wait_next) {
        count++;
    }
    return count;
}

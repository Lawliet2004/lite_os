#include <arch/x86_64/cpu.h>
#include <drivers/pit.h>
#include <kernel/printk.h>
#include <lib/string.h>
#include <sched/scheduler.h>
#include <sched/wait_queue.h>

static struct wait_queue io_event_wq;

void wait_queue_init(struct wait_queue *wq)
{
    if (wq == 0) return;
    wq->head = 0;
    wq->tail = 0;
}

struct wait_queue *wait_queue_io_event(void)
{
    return &io_event_wq;
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
    self->sleep_timed_out = false;

    schedule();

    self->wait_queue = 0;
    self->wait_next = 0;
}

bool wait_queue_sleep_timeout(struct wait_queue *wq, uint64_t timeout_ticks)
{
    if (wq == 0 || current_task == 0) return false;
    if (timeout_ticks == 0) return true;

    bool was_enabled = save_interrupts_and_disable();

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
    self->sleep_timed_out = false;

    if (timeout_ticks != UINT64_MAX) {
        self->sleep_until = pit_ticks() + timeout_ticks;
        sched_add_sleeper(self);
    } else {
        self->sleep_until = 0;
    }

    schedule();

    bool timed_out = self->sleep_timed_out;
    self->sleep_timed_out = false;

    if (timed_out && self->wait_queue != 0) {
        struct wait_queue *cur_wq = self->wait_queue;
        struct task *prev = 0;
        struct task *curr = cur_wq->head;
        while (curr != 0) {
            if (curr == self) {
                if (prev != 0) {
                    prev->wait_next = curr->wait_next;
                } else {
                    cur_wq->head = curr->wait_next;
                }
                if (cur_wq->tail == curr) {
                    cur_wq->tail = prev;
                }
                break;
            }
            prev = curr;
            curr = curr->wait_next;
        }
    }

    self->wait_queue = 0;
    self->wait_next = 0;
    self->sleep_until = 0;

    restore_interrupts(was_enabled);
    return timed_out;
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
        task->wait_queue = 0;

        if (task->state == TASK_SLEEPING) {
            if (task->sleep_until != 0) {
                sched_remove_sleeper(task);
                task->sleep_until = 0;
            }
            task->sleep_timed_out = false;
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
        task->wait_queue = 0;
        if (task->state == TASK_SLEEPING) {
            if (task->sleep_until != 0) {
                sched_remove_sleeper(task);
                task->sleep_until = 0;
            }
            task->sleep_timed_out = false;
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

void io_event_notify(void)
{
    wait_queue_wake_all(&io_event_wq);
}

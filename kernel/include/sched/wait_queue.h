#ifndef LITENIX_SCHED_WAIT_QUEUE_H
#define LITENIX_SCHED_WAIT_QUEUE_H

#include <sched/task.h>
#include <stdbool.h>
#include <stdint.h>

/*
 * Real blocking wait queues. Sleeping tasks are removed from the runqueue and
 * placed on a per-waitqueue list. The waker transitions each task back to
 * TASK_READY and re-inserts it into the runqueue. No polling, no spinning.
 *
 * Limitations:
 *   - single-CPU only
 *   - no timeout support (added in Phase 22)
 *   - waker runs with interrupts disabled briefly to avoid lost wakeups
 */
struct wait_queue {
    struct task *head;
    struct task *tail;
};

void wait_queue_init(struct wait_queue *wq);
void wait_queue_sleep(struct wait_queue *wq);
void wait_queue_sleep_locked(struct wait_queue *wq);
bool wait_queue_sleep_timeout(struct wait_queue *wq, uint64_t timeout_ticks);
void wait_queue_wake_one(struct wait_queue *wq);
void wait_queue_wake_all(struct wait_queue *wq);
uint64_t wait_queue_count(const struct wait_queue *wq);
struct wait_queue *wait_queue_io_event(void);
void io_event_notify(void);

#endif

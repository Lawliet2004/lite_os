#ifndef LITENIX_SCHED_SCHEDULER_H
#define LITENIX_SCHED_SCHEDULER_H

#include <stdint.h>
#include <stdbool.h>

struct task;

void sched_init(void);
void sched_tick(void);
void schedule(void);
bool sched_needs_resched(void);
void sched_add_task(struct task *task);
void sched_remove_task(struct task *task);
void sched_self_test(void);
void sched_dump(const char *label);
uint64_t sched_task_count(void);
struct task *sched_get_runqueue_head(void);

/* Phase 7: idle task and sleep list */
void sched_set_idle_task(struct task *task);
void sched_add_sleeper(struct task *task);
void sched_remove_sleeper(struct task *task);
void sched_check_sleepers(void);
void sched_boost_starved(void);
struct process;
void sched_exit_group(struct process *proc, int exit_code);

#endif

# Scheduler And Tasks

Phase 7 implements the Linux-like priority scheduler with sleep, wakeup, and
starvation prevention.

## Implemented

### Phase 6 Foundation

- `struct process` with PID, parent pointer, child list, main thread, exit
  state, and exit code
- `struct task` with PID, TID, kernel stack, saved stack pointer, task state,
  task mode, owning process, and parent task
- idle task creation during `task_init()`
- kernel thread creation through `task_create()`
- task exit to `TASK_ZOMBIE`
- parent wait and explicit reap through `task_wait()`
- stable scheduler task list; zombie tasks stay linked until reaped

### Phase 7 Priority Scheduler

- 40 priority levels via nice values -20 (highest) through +19 (lowest)
- time-slice proportional to priority: `SCHED_BASE_SLICE + (19 - nice)`
  - nice -20 → 40 ticks (400ms at 100 Hz)
  - nice 0 → 20 ticks (200ms)
  - nice +19 → 1 tick (10ms)
- priority-aware `pick_next_task()`: selects the READY task with the lowest
  nice value; round-robin among equal-priority tasks
- `sched_tick()` decrements time-slice; requests reschedule on expiry
- `task_set_priority()` / `task_get_priority()` APIs
- `task_sleep_ticks(N)` for timed sleep: task state set to SLEEPING, added
  to a sorted sleep list, woken by `sched_check_sleepers()` in `sched_tick()`
- dedicated idle task with `TASK_FLAG_IDLE` and priority +19; returned by
  `pick_next_task()` only when no other READY task exists
- per-task CPU accounting via `total_ticks` field
- starvation prevention: every 500 ticks, all READY tasks with priority above
  -20 are boosted by -1; priority resets to `static_priority` after running
- wait queues for blocking operations (reaper, general purpose)
- boot self-test with three scenarios:
  1. Fair-share: two equal-priority CPU-bound threads verify balanced ticks
  2. Priority: high-priority (-10) vs low-priority (+10) threads confirm
     priority ordering
  3. Sleep/Wake: thread sleeps 50 ticks and verifies correct wakeup

## Current Invariants

- a task may not be freed while it is `current_task`
- a zombie task remains allocated and visible to its parent until `task_wait()`
  reaps it
- scheduler selection skips zombies and sleeping tasks
- the idle task is never removed from the runqueue or put to sleep
- time-slice is reloaded from `time_slice_max` on expiry
- boosted priority is restored to `static_priority` after the boosted task runs
- timed sleepers remain in the runqueue with SLEEPING state; `pick_next_task`
  skips them; `sched_check_sleepers` sets them back to READY
- interrupt handlers keep their saved stack pointer in a callee-saved register,
  not a global, so nested task switches cannot corrupt a pending `iretq`
- hot scheduler paths do not allocate
- task stacks are owned by their task and are released during reap

## Not Implemented Yet

- runnable ring-3 user tasks
- per-process user address-space ownership beyond the structural fields
- blocking wait with timeout
- multicore locking (SMP)
- fair scheduling (CFS/EEVDF-inspired, deferred to later phases)
- realtime scheduling classes
- scheduler accounting beyond total_ticks

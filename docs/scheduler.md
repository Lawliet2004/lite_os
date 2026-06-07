# Scheduler

No scheduler exists in Phase 0/1.

Planned progression:

1. idle task and kernel threads
2. round-robin scheduler with priority
3. sleep and wake queues
4. fair scheduler using virtual runtime
5. scheduling classes and better latency accounting

The scheduler must avoid allocation in hot paths and must not run freed tasks.

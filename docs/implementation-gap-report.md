# LiteNix Implementation Gap Report

Last updated: 2026-06-11

This report is based on code inspection, not on a fresh boot verification run.

## Snapshot

- Syscall handlers found in code: 224
- Weak ENOSYS fallback handlers in `syscall_table.c`: 8
- Highest-risk compatibility gap addressed in this slice: event waiting

## Status Table

| Area | Status | Notes |
|---|---|---|
| `select` / `pselect6` | PARTIAL | Shared I/O event queue and timeout logic are in code; boot verification still pending |
| `epoll_wait` | PARTIAL | Uses the same event queue path; boot verification still pending |
| `mount` / `umount2` | STUB | Still returns success in the syscall layer |
| `MAP_SHARED` | STUB | File-backed shared mappings are still not implemented |
| `procfs` / `devfs` basics | DONE/PARTIAL | Present, but not all Linux semantics are complete |
| `AF_UNIX` IPC | PARTIAL | `socketpair` works; broader Unix-domain socket behavior remains incomplete |
| Installer | MISSING | No real on-disk install path yet |
| Desktop compositor | MISSING | No real native GUI stack yet |

## Next Highest-Leverage Slice

1. Boot-verify the event-waiting path.
2. Replace `mount` / `umount2` success stubs with real behavior or correct errno.
3. Keep extending `procfs`, `devfs`, and `tmpfs` support for real userspace.

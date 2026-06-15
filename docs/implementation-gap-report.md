# LiteNix Implementation Gap Report

Last updated: 2026-06-11

This report is based on code inspection, not on a fresh boot verification run.

## Snapshot

- Syscall handlers found in code: 224
- Weak ENOSYS fallback handlers in `syscall_table.c`: 8
- Highest-risk compatibility gap addressed in this slice: userspace mount reporting

## Status Table

| Area | Status | Notes |
|---|---|---|
| `select` / `pselect6` | PARTIAL | Shared I/O event queue and timeout logic are in code; boot verification still pending |
| `epoll_wait` | PARTIAL | Uses the same event queue path; boot verification still pending |
| `mount` / `umount2` | PARTIAL | Validates credentials, pointers, target paths, and flags; returns real errors or `-ENOSYS` instead of false success |
| `MAP_SHARED` | PARTIAL | Invalid requests now return normal errno first; valid shared mappings return `-ENOSYS` until shared page-cache semantics exist |
| `procfs` / `devfs` basics | DONE/PARTIAL | Present, including `/proc/mounts`; many Linux procfs/sysfs details remain incomplete |
| `AF_UNIX` IPC | PARTIAL | `socketpair` works; broader Unix-domain socket behavior remains incomplete |
| Installer | MISSING | No real on-disk install path yet |
| Desktop compositor | MISSING | No real native GUI stack yet |

## Next Highest-Leverage Slice

1. Boot-verify the event-waiting path.
2. Implement real userspace-driven `mount` / `umount2` once device-backed superblock setup and mount lifecycle ownership are ready.
3. Keep extending `procfs`, `devfs`, and `tmpfs` support for real userspace.

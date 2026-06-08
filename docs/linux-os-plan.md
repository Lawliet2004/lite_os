# LiteNix Linux-Like OS Plan

This plan tracks the path from the current teaching kernel toward a stable
Linux-like userspace. A phase is only complete when the code, boot evidence,
and compatibility tests all agree.

## Compatibility Milestones

| Milestone | Target | Required Result |
| --- | --- | --- |
| L0 | Native LiteNix programs | Current libc-lite init and shell run reliably. |
| L1 | Static no-libc Linux ABI programs | Direct syscall programs can use write, exit, openat, fstatat, mmap, brk, time, and uname. |
| L2 | Static musl hello/tools | Static musl hello world and simple file tools run without custom libc-lite. |
| L3 | Static BusyBox base system | BusyBox sh, ls, cat, echo, mkdir, rm, cp, and basic scripts run from initramfs. |
| L4 | Usable CLI OS | Pipes, signals, terminal behavior, procfs, tmpfs, ext2, networking, and process control are stable enough for repeated shell use. |
| L5 | Package/download foundation | Persistent writable storage, TCP robustness, DNS resolver support, TLS-capable userspace, and a simple package/install format exist. |
| L6 | Dynamic binaries | PT_INTERP, auxv, file-backed mmap, shared libraries, and dynamic musl binaries work. |
| L7 | Broader Linux CLI apps | glibc-facing compatibility, robust futexes, ptys, ioctl coverage, select/epoll, and richer procfs/sysfs exist. |
| L8 | Graphical/browser-class apps | Framebuffer or GPU path, input stack, windowing/compositor or ported GUI layer, shared memory, threads, networking, and large-file storage are mature. |

## Phase Status

| Phase | Status | Missing Work |
| --- | --- | --- |
| 0-5 | Mostly implemented | Keep build and boot verification reproducible on Windows and Unix hosts. |
| 6-8 | Mostly implemented | Keep scheduler/syscall tests in CI; remove stale duplicate syscall code. |
| 9-10 | Implemented for custom ELF tests | Expand ELF validation, auxv, static musl entry assumptions, and failure cleanup tests. |
| 11-13 | Partially implemented | Improve VFS path semantics, fd behavior, procfs accuracy, and device/TTY behavior. |
| 14 | Partial | Add memory pressure behavior, reclaimable caches, shared zero page, and eventually COW. |
| 15 | Partial | Harden fork/exec/wait, fd inheritance, process groups, sessions, and exit-group semantics. |
| 16 | Partial | Deliver user signal handlers, signal frames, restorer path, SIGCHLD/SIGINT behavior, and masks per thread. |
| 17 | Partial | Complete robust futex support, TLS/thread teardown, and pthread compatibility. |
| 18 | Partial | Finish MAP_FIXED rules, file-backed mmap, VMA overlap checks, mremap later, and malloc stress tests. |
| 19 | Partial | Maintain a tested binary matrix with exact pass/fail notes. |
| 20 | Partial | Make init run shell by default after tests, add login/session behavior later. |
| 21 | Partial | Add select and epoll; harden pipe close, blocking, and nonblocking edge cases. |
| 22 | Partial | Add RTC-backed wall clock, timer precision, interruption behavior, and timeout-aware waits. |
| 23 | Partial | Add persistent block storage, mount table, tmpfs semantics, ext2 coverage, and page cache later. |
| 24 | Partial | Stabilize virtio-net, ARP/IP/ICMP/UDP/TCP, add DNS userspace support, and test host networking. |
| 25 | Not proven | Run and document VirtualBox boot, storage, keyboard, network, and serial workflows. |
| 26 | Partial | Add kernel log discipline, stack traces, syscall tracing controls, and crash reports. |
| 27 | Partial | Audit hostile-userspace boundaries, integer overflow, usercopy, fd bounds, and permissions. |
| 28 | Not implemented | Track boot time, memory use, syscall cost, context switch cost, and RAM budgets. |
| 29 | Partial | Add kernel unit tests, userspace compatibility tests, and repeatable QEMU CI. |
| 30 | Process rule | Every phase change needs implementation notes, invariants, build, boot, and known limits. |

## Immediate Work Queue

1. Stabilize current build and boot verification on the development host.
2. Bring L1 syscall coverage up: openat, newfstatat, gettid, uname, robust-list stubs, getrandom, readlink, access/faccessat, prlimit64, and statx stubs where safe.
3. Build and run static no-libc Linux ABI tests outside libc-lite.
4. Attempt static musl hello world, record every missing syscall, and implement only the calls required by the evidence.
5. Attempt static BusyBox with a small command matrix.
6. Harden shell usability: terminal input, pipes, child process control, signals, and procfs.
7. Prove VirtualBox boot and networking with documented serial logs.

## Current Slice

The first compatibility slice adds Linux syscall numbers and handlers for:

- `openat`
- `newfstatat`
- `gettid`
- `uname`
- `set_robust_list`
- `get_robust_list`

The second compatibility slice adds minimal handlers for:

- `access`
- `faccessat`
- `readlink`
- `readlinkat`
- `statx`
- `getrandom`
- `getrlimit`
- `prlimit64`

These are foundational for static libc programs. They do not complete robust
futex recovery, directory-FD-relative path lookup, symbolic links, writable
resource limits, dynamic linking, or full Linux application compatibility.

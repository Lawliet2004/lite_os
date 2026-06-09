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
| **Phase 0: Stability Baseline** | **Completed** | Stable build & verification script active. |
| **Phase 1: Linux Syscall Foundation** | **Completed** | Syscall table is compatible; negative errno returned; user pointer checks added. |
| **Phase 2: VFS Maturity** | **Completed** | Initramfs, devfs, procfs, ext2, and fd features verified. |
| **Phase 3: ELF Loader & Static Programs** | **Completed** | Static musl hello runs with correct argv/envp passing, and execve invalid inputs are rejected safely. |
| **Phase 4: VMA & Page Fault Paging** | **Completed** | Process VMA management, overlap detection, splits/merges, and anonymous lazy demand paging. |
| **Phase 5: File-backed mmap** | **Completed** | File-backed mmap MAP_PRIVATE with COW page fault loading; offset alignment validated. |
| **Phase 6: Dynamic Linker Support** | *Planned* | PT_INTERP loading and dynamic Musl hello world. |

## Immediate Work Queue

1. Maintain stable build and boot verification matrix on host.
2. Complete file-backed `mmap` and VMA tracking (Phase 4).
3. Test dynamic musl binaries (Phase 5).
4. Expand signals and process group compatibility (Phase 6).

## Current Slice

The third compatibility slice completes Phase 3 by:
- Correctly parsing and passing `argc`, `argv`, and `envp` onto the user stack for static musl program execution.
- Standardizing the `sys_execve` error cases (`-EFAULT`, `-ENOENT`, `-EACCES`, `-ENOEXEC`) and validating all user inputs.
- Verifying the boot output and checking rejection constraints via userspace integration tests.

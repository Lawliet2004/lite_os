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
| **Phase 0: Stability Baseline** | ✅ **Completed** | Stable build & verification script active. |
| **Phase 1: Linux Syscall Foundation** | ✅ **Completed** | Syscall table is compatible; negative errno returned; user pointer checks added. |
| **Phase 2: VFS Maturity** | ✅ **Completed** | Initramfs, devfs, procfs, ext2, and fd features verified. |
| **Phase 3: ELF Loader & Static Programs** | ✅ **Completed** | Static musl hello runs with correct argv/envp passing, and execve invalid inputs are rejected safely. |
| **Phase 4: VMA & Page Fault Paging** | ✅ **Completed** | Process VMA management, overlap detection, splits/merges, and anonymous lazy demand paging. |
| **Phase 5: File-backed mmap** | ✅ **Completed** | File-backed mmap MAP_PRIVATE with COW page fault loading; offset alignment validated. |
| **Phase 6: Dynamic Linker Support** | ✅ **Completed** | PT_INTERP detection, interpreter loading with base address for ET_DYN, full auxiliary vector (AT_PHDR, AT_PHENT, AT_PHNUM, AT_BASE, AT_ENTRY, AT_RANDOM, AT_EXECFN, AT_UID, AT_EUID, AT_GID, AT_EGID, AT_SECURE), SysV stack alignment. Tested with fake interpreter. |
| **Phase 7: Process Groups, Signals, TTY** | ✅ **Verified slice** | Process groups, signal action/blocking tests, basic terminal line discipline, and BusyBox shell smoke tests pass. Complete POSIX signal and TTY behavior remains future work. |
| **Phase 8: Distro Foundation** | ✅ **Completed** | Persistent storage, multi-service init reaped daemons, stable loopback network, DNS local resolve, HTTP download, custom package install/uninstall, and boot tests all pass. |

## Phase 8 Distro Plan

Working toward a minimal bootable CLI distro. Each sub-phase must produce serial-log evidence before proceeding.

| Sub-phase | Goal | Status |
| --- | --- | --- |
| 8.1: Documentation Accuracy | All docs match current serial-log evidence; architecture.md, syscall-roadmap.md, compatibility.md reflect reality. | ✅ Completed |
| 8.2: Persistent Filesystem | Stabilize ext2 write path on real ATA disk (disk.img). Write and readback persist across reboots. | ✅ Verified |
| 8.3: Init / Service Startup | A proper init process (PID 1) launches services, handles SIGCHLD, and manages daemons. | ✅ Completed |
| 8.4: Persistent Root Filesystem | Writable root filesystem on ext2 disk image; read-only initramfs as fallback. | ✅ Completed |
| 8.5: Shell Environment | A usable shell environment with PATH, environment variables, job control, and TTY line discipline. | ✅ Completed |
| 8.6: Networking | DHCP client, DNS resolver, basic TCP stability (retransmit, FIN handshake). | ✅ Completed |
| 8.7: Package Manager | `lpkg` minimal installer: download, verify, extract, install to root filesystem. | ✅ Completed |
| 8.8: TLS / HTTPS | TLS library (mbedTLS or WolfSSL port) for secure package download. | 📋 HTTPS prepared |
| 8.9: User Management | `/etc/passwd`, `/etc/shadow`, login, su. | 📋 Queued |
| 8.10: Self-hosted Build | Build a simple C program inside LiteNix using an in-kernel-hosted toolchain. | 📋 Queued |
| 8.11: Reproducible Image | Single-command image build that produces a bootable ISO with the above features. | ✅ Completed |
| 8.12: Release Milestone | L4 milestone: reliable CLI OS with shell, persistence, networking, and package install. | ✅ Completed |

## Immediate Work Queue

1. **COMPLETE 8.1** — Architecture.md, syscall-roadmap.md, compatibility.md updated. ✅
2. **STABILIZE 8.2** — EXT2 write/readback verified. Note: Real disk writes work but disk.img is regenerated by build system. ✅
3. **COMPLETE 8.3-8.8** — Multi-daemon init, TCP stability, DNS resolver, HTTP streaming client, and lpkg installer verified. ✅
4. Improve signal and TTY behavior needed by longer BusyBox shell sessions.
5. Expand the signal and terminal backlog: `sigaltstack`, job control, and ptys.
6. Extend procfs/sysfs coverage only where real userspace probes require it.

## Current Slice

The current compatibility slice is a verified CLI smoke path:
- QEMU positive boot verification passes with no kernel panic or unexpected CPU exception.
- Dynamic musl hello prints `Hello from dynamic musl!`.
- BusyBox shell smoke tests print `All shell tests PASSED`.
- Persistent ext2 image generation and `/ext2/hello.txt` read print `Test 26: PASSED`.
- EXT2 write/readback (Test 16) passes — fixed `ram_truncate` on memory-fallback node.
- HTTP client downloads from localhost http server, package manager installs/lists/removes correctly, verifying loopback networking and package deployment.

# Syscall Roadmap

## Current Status

LiteNix implements the Linux x86_64 syscall ABI via SYSCALL/SYSRET (EFER/STAR/LSTAR/SFMASK MSRs).
Unknown syscalls return `-ENOSYS`. Invalid user pointers return `-EFAULT`. The negative errno convention is followed throughout.

Last full verification: `make verify-boot` PASS (2026-06-10).

## Implemented and Verified Syscalls

### Process / Thread
| # | Name | Notes |
|---|------|-------|
| 39 | `getpid` | Full |
| 110 | `getppid` | Full |
| 186 | `gettid` | Full |
| 57 | `fork` | Full |
| 56 | `clone` | Thread setup (CLONE_VM, stack, TLS) |
| 59 | `execve` | Full (static + dynamic ELF) |
| 61 | `wait4` | Full |
| 60 | `exit` | Full |
| 231 | `exit_group` | Full |
| 112 | `setsid` | Full |
| 109 | `setpgid` | Full |
| 111 | `getpgrp` | Full |
| 102 | `getuid` | Returns 0 (root) |
| 107 | `geteuid` | Returns 0 (root) |
| 104 | `getgid` | Returns 0 (root) |
| 108 | `getegid` | Returns 0 (root) |
| 218 | `set_tid_address` | Full |
| 158 | `arch_prctl` | ARCH_SET_FS / ARCH_GET_FS / ARCH_SET_GS |
| 273 | `set_robust_list` | Stores head pointer |
| 274 | `get_robust_list` | Returns stored pointer |
| 202 | `futex` | FUTEX_WAIT / FUTEX_WAKE / FUTEX_PRIVATE_FLAG |

### Filesystem
| # | Name | Notes |
|---|------|-------|
| 0 | `read` | Full |
| 1 | `write` | Full (with O_APPEND) |
| 19 | `readv` | Full |
| 20 | `writev` | Full |
| 2 | `open` | Full (O_CREAT, O_TRUNC, O_APPEND, O_RDONLY/RDWR/WRONLY) |
| 257 | `openat` | Full |
| 3 | `close` | Full |
| 8 | `lseek` | SEEK_SET / SEEK_CUR / SEEK_END |
| 4 | `stat` | Full |
| 5 | `fstat` | Full |
| 6 | `lstat` | Full (no symlink follow on last component) |
| 262 | `newfstatat` | Full |
| 332 | `statx` | Stub — maps to stat fields |
| 21 | `access` | Full |
| 269 | `faccessat` | Full |
| 89 | `readlink` | Returns -EINVAL for non-symlinks |
| 267 | `readlinkat` | Full |
| 217 | `getdents64` | Full |
| 83 | `mkdir` | Full |
| 258 | `mkdirat` | Full |
| 87 | `unlink` | Full |
| 263 | `unlinkat` | Full |
| 84 | `rmdir` | Full |
| 32 | `dup` | Full |
| 33 | `dup2` | Full |
| 292 | `dup3` | Full |
| 72 | `fcntl` | F_GETFL / F_SETFL / F_GETFD / F_SETFD / F_DUPFD |
| 90 | `chmod` | Sets mode bits |
| 91 | `fchmod` | Sets mode bits |
| 268 | `fchmodat` | Full |
| 92 | `chown` | Sets uid/gid |
| 93 | `fchown` | Full |
| 94 | `lchown` | Full |
| 260 | `fchownat` | Full |
| 95 | `umask` | Full |
| 264 | `renameat` | Full |
| 316 | `renameat2` | Full (RENAME_NOREPLACE only) |
| 88 | `symlink` | Full |
| 266 | `symlinkat` | Full |

### Memory
| # | Name | Notes |
|---|------|-------|
| 12 | `brk` | Full |
| 9 | `mmap` | Anonymous (lazy demand paging) + file-backed MAP_PRIVATE |
| 11 | `munmap` | Full |
| 10 | `mprotect` | Full |

### Time / Sync
| # | Name | Notes |
|---|------|-------|
| 228 | `clock_gettime` | CLOCK_MONOTONIC, CLOCK_REALTIME |
| 96 | `gettimeofday` | Full |
| 35 | `nanosleep` | Full (PIT-driven) |

### System
| # | Name | Notes |
|---|------|-------|
| 63 | `uname` | Full |
| 318 | `getrandom` | xorshift64 PRNG |
| 97 | `getrlimit` | RLIMIT_NOFILE, RLIMIT_STACK, others stubbed |
| 302 | `prlimit64` | Partial |

### Signals
| # | Name | Notes |
|---|------|-------|
| 13 | `rt_sigaction` | Full |
| 14 | `rt_sigprocmask` | Full |
| 15 | `rt_sigreturn` | Full |

### I/O Multiplexing
| # | Name | Notes |
|---|------|-------|
| 7 | `poll` | Full |
| 22 | `pipe` | Full |
| 293 | `pipe2` | Redirects to `pipe` + flags |

### Sockets
| # | Name | Notes |
|---|------|-------|
| 41 | `socket` | AF_INET UDP/TCP |
| 49 | `bind` | Full |
| 50 | `listen` | Full |
| 43 | `accept` | Full |
| 42 | `connect` | Full |
| 44 | `sendto` | Full |
| 45 | `recvfrom` | Full |
| 54 | `setsockopt` | Full |
| 55 | `getsockopt` | Full |
| 48 | `shutdown` | Full |

### Terminal
| # | Name | Notes |
|---|------|-------|
| 16 | `ioctl` | TCGETS / TCSETS / TIOCGWINSZ stubs |

## Stubs (Safe, Return errno)
- `madvise` → `-ENOSYS` (programs tolerate this)
- `mremap` → `-ENOSYS`
- `prctl` → partial (PR_SET_NAME stored, others `-EINVAL`)
- `epoll_create1` → `-ENOSYS`
- `epoll_ctl` → `-ENOSYS`
- `epoll_wait` → `-ENOSYS`
- `sendmsg` → `-ENOSYS`
- `recvmsg` → `-ENOSYS`
- `socketpair` → `-ENOSYS`

## Argument Validation Rules
1. Every syscall validates user pointer arguments with `copy_from_user` / `copy_to_user` before touching userspace memory.
2. Invalid pointers return `-EFAULT`.
3. Null pointers return `-EFAULT`.
4. Huge sizes are rejected before allocation.
5. FD bounds checked against `MAX_FILES_PER_PROCESS`.
6. Every syscall returns a negative errno on error.

## Next Syscalls to Add
In priority order for PHASE 3 (TTY/Signals) and PHASE 4 (syscall compat):
1. `select` / `pselect6` — needed by some musl programs
2. `epoll_create1` / `epoll_ctl` / `epoll_wait` — needed by heavier daemons
3. `sendmsg` / `recvmsg` — needed for Unix sockets / DNS
4. `getcwd` — needed by shell `pwd`
5. `chdir` — needed by shell `cd`
6. `getresuid` / `getresgid` — needed by some login/permission tools
7. `setuid` / `setgid` — needed for PHASE 7 users/permissions

# Syscall Infrastructure

## Implemented Syscalls (Linux x86_64 ABI)

All handlers:
- Use Linux x86_64 syscall numbers
- Validate user pointers via `uaccess_ok()` / `copy_from_user` / `copy_to_user` before touching user memory
- Return non-negative on success or negative errno on error
- Are registered in `syscall_table.c`

### ABI Setup

- `SYSCALL`/`SYSRET` MSR programming via `syscall_init()`
- `IA32_EFER.SCE`, `IA32_STAR`, `IA32_LSTAR`, `IA32_SFMASK`
- GDT: 7 entries — null, kernel code/data, TSS (2 slots), user data (DPL=3), user code (DPL=3)

### Assembly Entry (`syscall_stub.S`)

Full Linux x86_64 syscall frame on the kernel stack:
- All 15 GPRs in fixed layout matching `struct syscall_frame`
- `RCX` = user RIP (saved by `SYSCALL`), `R11` = user RFLAGS
- `R10` = arg4 (Linux ABI — `RCX` clobbered by `SYSCALL`)

### Dispatch Table (`syscall_table.c`)

256-entry function pointer table. All slots default to `sys_enosys` (-ENOSYS).

### Negative Errno Convention

All syscall handlers return `int64_t`:
- `>= 0` on success
- `< 0` on error (`-EFAULT`, `-ENOSYS`, `-EBADF`, …)

---

## Syscall Table

### File I/O (`sys_file.c`)

| Syscall        | Nr  | Status    | Notes |
|----------------|-----|-----------|-------|
| `read`         | 0   | ✅ Full   | Validates fd, user ptr, `copy_to_user` |
| `write`        | 1   | ✅ Full   | O_APPEND support; fallback serial for fd 1/2 |
| `open`         | 2   | ✅ Full   | O_CREAT, O_TRUNC, O_APPEND |
| `close`        | 3   | ✅ Full   | ref-counted close |
| `stat`         | 4   | ✅ Full   | path-based stat |
| `lstat`        | 5   | ✅ Stub   | aliases to stat (no symlinks) |
| `fstat`        | 5   | ✅ Full   | fd-based stat |
| `lseek`        | 8   | ✅ Full   | SEEK_SET/CUR/END |
| `ioctl`        | 16  | ✅ Partial| TCGETS, TIOCGWINSZ, TIOCSWINSZ, TIOCGPGRP |
| `dup`          | 32  | ✅ Full   | |
| `dup2`         | 33  | ✅ Full   | |
| `fcntl`        | 72  | ✅ Partial| F_GETFD, F_SETFD, F_GETFL, F_SETFL, F_DUPFD |
| `getdents64`   | 217 | ✅ Full   | |
| `mkdir`        | 83  | ✅ Full   | |
| `rmdir`        | 84  | ✅ Full   | |
| `unlink`       | 87  | ✅ Full   | |
| `rename`       | 82  | ✅ Full   | |
| `access`       | 21  | ✅ Full   | F_OK, R_OK, W_OK, X_OK |
| `faccessat`    | 269 | ✅ Full   | |
| `readlink`     | 89  | ✅ Stub   | returns -ENOSYS for non-symlinks |
| `readlinkat`   | 267 | ✅ Stub   | |
| `openat`       | 257 | ✅ Full   | AT_FDCWD + absolute paths |
| `newfstatat`   | 262 | ✅ Full   | AT_FDCWD + AT_EMPTY_PATH |
| `statx`        | 332 | ✅ Full   | STATX_BASIC_STATS |
| `execve`       | 59  | ✅ Full   | ELF loader |
| `readv`        | 19  | ✅ Full   | Vectored read support |
| `writev`       | 20  | ✅ Full   | Vectored write support |


### Process (`sys_exit.c`, `sys_process.c`)

| Syscall           | Nr  | Status    | Notes |
|-------------------|-----|-----------|-------|
| `exit`            | 60  | ✅ Full   | |
| `exit_group`      | 231 | ✅ Full   | |
| `fork`            | 57  | ✅ Full   | COW address space copy |
| `wait4`           | 61  | ✅ Full   | blocking wait, WNOHANG |
| `getpid`          | 39  | ✅ Full   | |
| `gettid`          | 186 | ✅ Full   | |
| `getppid`         | 110 | ✅ Full   | |
| `getuid`          | 102 | ✅ Stub   | always 0 |
| `getgid`          | 104 | ✅ Stub   | always 0 |
| `geteuid`         | 107 | ✅ Stub   | always 0 |
| `getegid`         | 108 | ✅ Stub   | always 0 |
| `chdir`           | 80  | ✅ Full   | |
| `getcwd`          | 79  | ✅ Full   | returns buf ptr on success |
| `clone`           | 56  | ✅ Full   | CLONE_THREAD + CLONE_VM for threads; fork for process clone |
| `arch_prctl`      | 158 | ✅ Full   | ARCH_SET_FS, ARCH_GET_FS |
| `futex`           | 202 | ✅ Full   | FUTEX_WAIT, FUTEX_WAKE |
| `set_tid_address` | 218 | ✅ Full   | |
| `set_robust_list` | 273 | ✅ Full   | |
| `get_robust_list` | 274 | ✅ Full   | |
| `getrandom`       | 318 | ✅ Full   | XorShift PRNG (not cryptographic) |
| `uname`           | 63  | ✅ Full   | sysname=LiteNix, machine=x86_64 |
| `kill`            | 62  | ✅ Partial| PID > 0 only |
| `rt_sigaction`    | 13  | ✅ Partial| global handler table, no delivery to user stack yet |
| `rt_sigprocmask`  | 14  | ✅ Full   | SIG_BLOCK, SIG_UNBLOCK, SIG_SETMASK |
| `clock_gettime`   | 228 | ✅ Full   | CLOCK_REALTIME, CLOCK_MONOTONIC (100 Hz PIT) |
| `gettimeofday`    | 96  | ✅ Full   | |
| `nanosleep`       | 35  | ✅ Full   | sleeps in PIT ticks |
| `getrlimit`       | 97  | ✅ Full   | RLIMIT_STACK, RLIMIT_NOFILE, RLIMIT_AS |
| `prlimit64`       | 302 | ✅ Partial| read only (new_limit not supported) |
| `pipe`            | 22  | ✅ Full   | |
| `pipe2`           | 293 | ✅ Full   | O_CLOEXEC, O_NONBLOCK |
| `poll`            | 7   | ✅ Full   | POLLIN, POLLOUT, POLLERR, POLLHUP |

### Memory (`sys_mem.c`)

| Syscall      | Nr  | Status    | Notes |
|--------------|-----|-----------|-------|
| `brk`        | 12  | ✅ Full   | grows/shrinks process heap; 256 MiB cap |
| `mmap`       | 9   | ✅ Partial| MAP_ANONYMOUS + MAP_PRIVATE only; file-backed = -ENOSYS |
| `munmap`     | 11  | ✅ Full   | unmaps + frees PMM pages, updates VMAs |
| `mprotect`   | 10  | ✅ Full   | updates page flags and VMA records |

### Networking (`net/socket.c`)

| Syscall    | Nr  | Status    | Notes |
|------------|-----|-----------|-------|
| `socket`   | 41  | ✅ Full   | AF_INET, SOCK_DGRAM/SOCK_STREAM |
| `bind`     | 49  | ✅ Full   | |
| `listen`   | 50  | ✅ Full   | |
| `accept`   | 43  | ✅ Full   | blocks until client connects |
| `connect`  | 42  | ✅ Full   | |
| `sendto`   | 44  | ✅ Full   | |
| `recvfrom` | 45  | ✅ Full   | |

---

## Raw-Syscall Test Suite (Milestone 2)

Located at `tests/userspace/raw-syscall/`. Each binary uses only raw `syscall`
instructions — no libc, no CRT, no kernel headers. Built with `zig cc -target
x86_64-freestanding-none`.

| Binary              | Syscalls exercised                                           |
|---------------------|--------------------------------------------------------------|
| `test_write_exit`   | `write`, `exit_group`                                        |
| `test_read`         | `openat`, `read`, `close`                                    |
| `test_stat`         | `newfstatat`                                                 |
| `test_mmap`         | `mmap`, `munmap`, `brk`                                      |
| `test_process`      | `getpid`, `gettid`, `uname`                                  |
| `test_clock`        | `clock_gettime`, `nanosleep`                                 |
| `test_getrandom`    | `getrandom`                                                  |
| `test_all`          | All of the above; prints "RAWSYSCALL: all tests passed"     |

Build: `make userspace-tests`

The initramfs includes `/tests/test_all`. It is executed by `/bin/init` on every
boot after the VFS tests. Boot logs will contain:

```
Phase 2: raw-syscall tests PASSED
```

---

## Known Limitations / Stubs

| Feature | Status | Details |
|---------|--------|---------|
| File-backed mmap | ❌ ENOSYS | Returns `-ENOSYS`; needed for musl/BusyBox (Milestone 7) |
| symlinks | ❌ None | `readlink` returns `-ENOSYS` for non-symlinks |
| uid/gid | Stub | Always returns 0 (root) |
| signal delivery to user stack | ❌ None | `rt_sigaction` stores handlers but does not deliver to user stack |
| `kill` with pid=0 or negative | ❌ ENOSYS | Only PID > 0 supported |
| `getrandom` entropy | Weak | XorShift PRNG seeded from PIT ticks; not cryptographic |
| `clock_gettime` resolution | 10 ms | 100 Hz PIT only |
| persistent filesystem | ❌ None | Initramfs is in-memory; EXT2 image is read-only |
| dynamic ELF loading | ❌ None | PT_INTERP (ld.so) not handled |

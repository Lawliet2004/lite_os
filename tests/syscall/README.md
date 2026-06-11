# Syscall Tests

This directory contains userspace programs that test individual LiteNix kernel syscalls.
Each test is a standalone **freestanding** C program with no libc dependencies — it issues
syscalls directly via inline assembly following the Linux x86-64 ABI.

## Test Files

| File | Syscalls Tested |
|------|----------------|
| `test_credentials.c` | `getuid`, `geteuid`, `getgid`, `getegid`, `getresuid`, `setresuid`, `getresgid`, `setresgid`, `prctl` |
| `test_epoll.c` | `epoll_create1`, `epoll_ctl`, `epoll_wait` |
| `test_select.c` | `select`, `pselect6` |
| `test_mremap.c` | `mmap`, `mremap`, `madvise`, `munmap` |
| `test_kill_pgrp.c` | `kill(0,sig)`, `kill(-pgid,sig)`, `getpgid` |
| `test_socket_ext.c` | `socket`, `setsockopt`, `getsockopt`, `shutdown` |
| `test_socketpair_msg.c` | `socketpair`, `sendmsg`, `recvmsg` |

## Build

All test programs are freestanding (no libc). Build with a cross-compiler targeting the kernel ABI:

```sh
x86_64-linux-musl-gcc -static -nostdlib -o test_credentials  test_credentials.c
x86_64-linux-musl-gcc -static -nostdlib -o test_epoll        test_epoll.c
x86_64-linux-musl-gcc -static -nostdlib -o test_select       test_select.c
x86_64-linux-musl-gcc -static -nostdlib -o test_mremap       test_mremap.c
x86_64-linux-musl-gcc -static -nostdlib -o test_kill_pgrp    test_kill_pgrp.c
x86_64-linux-musl-gcc -static -nostdlib -o test_socket_ext   test_socket_ext.c
```

Or using a native compiler on Linux:

```sh
gcc -static -nostdlib -o test_X test_X.c
```

> **Note:** Do not use `-nostartfiles` alone — these programs define `_start` directly,
> so the entire CRT must be omitted (`-nostdlib`).

## Expected Output

Each test prints one line per case:

```
[ OK ] description    ← test passed
[FAIL] description    ← test failed
```

A successful run exits with code `0`. An early-exit failure uses code `1`.

## Design Notes

- **No libc**: All I/O uses `write(1, ...)` via raw syscall wrappers.
- **No global state**: All variables are stack-local inside `_start`.
- **ABI compliance**: Arguments follow the Linux x86-64 calling convention
  (`rdi`, `rsi`, `rdx`, `r10`, `r8`, `r9`; `rax` for syscall number and return value).
- **Signal 0 trick**: `kill(pid, 0)` is used in `test_kill_pgrp.c` as a non-destructive
  existence/permission check — it never actually delivers a signal.

# Syscall Roadmap

Phase 0/1 has no syscall support.

When syscall work begins, LiteNix should use Linux x86_64 syscall numbers for
compatibility targets. Unknown syscalls must return `-ENOSYS`, and invalid user
pointers must return `-EFAULT`.

## First User Program Target

Minimum for a tiny no-libc program:

- `write`
- `exit`

## Static Musl Hello Target

Likely additions:

- `read`
- `openat`
- `close`
- `brk`
- `mmap`
- `munmap`
- `mprotect`
- `fstat`
- `newfstatat`
- `getpid`
- `gettid`
- `clock_gettime`
- `arch_prctl`
- `set_tid_address`
- `set_robust_list`
- `rt_sigaction` stub
- `rt_sigprocmask` stub

Every syscall must validate arguments and user pointers before touching
userspace memory.

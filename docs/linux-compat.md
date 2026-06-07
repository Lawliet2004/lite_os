# Linux Compatibility

LiteNix must not claim support for all Linux applications. Compatibility is a
measured property backed by binaries that actually run under LiteNix.

## Compatibility Levels

| Level | Target | Required Evidence |
| --- | --- | --- |
| 0 | custom user programs only | `write` and `exit` equivalents work |
| 1 | static no-libc programs | tiny syscall-wrapper programs run |
| 2 | static musl hello world | ELF loader, stack setup, basic syscalls |
| 3 | static BusyBox | initramfs root, shell, basic VFS and devfs |
| 4 | BusyBox shell scripts | `fork`, `exec`, `wait`, `pipe`, `dup`, stat calls |
| 5 | coreutils subset | procfs basics, signals, terminal basics |
| 6 | dynamic musl binaries | `PT_INTERP`, auxv, file-backed `mmap` |
| 7 | simple glibc binaries | TLS, robust futex basics, time and uname syscalls |
| 8 | pthread programs | `clone`, futex, TLS, signal handlers |
| 9 | network CLI apps | sockets, TCP/IP, `poll`/`select`/`epoll` |
| 10 | complex CLI apps | broader `/proc`, `/sys`, pty, ioctl compatibility |

Level 10 is not a promise. It can only be claimed after the compatibility test
matrix passes.

## Test Matrix

Initial matrix:

- `hello-static`
- `hello-musl-static`
- `busybox-static`
- `sh`
- `ls`
- `cat`
- `echo`
- `grep`
- `sed`
- `awk`
- `true`
- `false`
- `yes`
- `head`
- `tail`
- pthread mutex test
- sqlite CLI later
- curl later
- nano or vim later
- Python later

Graphical Linux applications are outside the early scope.

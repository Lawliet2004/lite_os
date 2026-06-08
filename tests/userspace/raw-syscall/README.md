# raw-syscall — No-libc Linux ABI Test Suite

Each program uses only raw `syscall` instructions — no libc, no CRT.
Built with `zig cc -target x86_64-freestanding-none` + custom linker script.

## Tests

| Binary              | Syscalls exercised                                       |
|---------------------|----------------------------------------------------------|
| `test_write_exit`   | `write(1)`, `exit_group`                                 |
| `test_read`         | `openat`, `read`, `close`                                |
| `test_stat`         | `newfstatat`                                             |
| `test_mmap`         | `mmap`, `munmap`, `brk`                                  |
| `test_process`      | `getpid`, `gettid`, `uname`                              |
| `test_clock`        | `clock_gettime(CLOCK_MONOTONIC)`, `clock_gettime(CLOCK_REALTIME)` |
| `test_getrandom`    | `getrandom`                                              |
| `test_all`          | All of the above in sequence; exits 0 on full pass       |

## Building (on the host)

```
make userspace-tests
```

## Running (in QEMU)

The initramfs includes these binaries under `/tests/`. The init process
runs `test_all` at boot and prints `RAWSYSCALL: all tests passed`.
A missing syscall returns `-ENOSYS` (-38) and the test prints which one failed.

## Syscall Calling Convention (x86_64 Linux)

```
rax = syscall number
rdi = arg1
rsi = arg2
rdx = arg3
r10 = arg4
r8  = arg5
r9  = arg6
syscall
; result in rax (negative = -errno)
```

## Known Limitations

- `mmap` only supports `MAP_ANONYMOUS|MAP_PRIVATE` (file-backed: `-ENOSYS`)
- `getrandom` uses a XorShift PRNG seeded from PIT ticks (not cryptographic)
- `clock_gettime` resolution is ~10 ms (100 Hz PIT)
- `uname` returns `LiteNix` / `0.1.0` / `x86_64`

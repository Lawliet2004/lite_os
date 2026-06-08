# LiteNix Linux ABI Compatibility Matrix

This matrix documents the verification status of various Linux userspace programs and test suites under LiteNix. It is updated at each phase to honestly track what works, what fails, and what is missing.

## Compatibility Matrix

| Program / Test Suite | Binary Type | Build Method | Status | Missing Syscalls / Features | Crash/Panic Status | Last Verified |
|----------------------|-------------|--------------|--------|-----------------------------|--------------------|---------------|
| **Raw Syscall Suite** (`/tests/test_all`) | Static ELF64 (no libc) | `tests/userspace/raw-syscall/*.c` compiled with no libc, custom entry | **PASS** | None | None | Phase 2 Baseline |
| **Static Musl Hello** (`/bin/hello_musl`) | Static ELF64 (musl) | `zig cc -target x86_64-linux-musl -static hello_musl.c` | **PASS** | None (uses `writev`, `exit_group`, `readv`) | None | Phase 3 Baseline |
| **Static BusyBox** | Static ELF64 (musl) | Precompiled static BusyBox | *Target (Phase 5/6)* | Process groups, signals, full fd inheritance, `statx` completeness | N/A | Not Yet Tested |
| **Dynamic Musl Hello** | Dynamic ELF64 | `zig cc -target x86_64-linux-musl hello_musl.c` | *Target (Phase 5)* | `PT_INTERP` loader, auxiliary vectors, file-backed `mmap` | N/A | Not Yet Tested |
| **BusyBox Shell** | Static ELF64 (musl) | Static musl compilation | *Target (Phase 6)* | Terminals, job control, signals | N/A | Not Yet Tested |

---

## Syscall Support Summary for Static Binaries

The following system calls have been verified and validated against bad user pointer inputs:

- **Process / Thread**: `exit` (60), `exit_group` (231), `getpid` (39), `gettid` (186), `getppid` (110), `fork` (57), `wait4` (61), `clone` (56 - thread setup), `set_tid_address` (218).
- **Filesystem**: `read` (0), `write` (1), `readv` (19), `writev` (20), `open` (2), `openat` (257), `close` (3), `lseek` (8), `stat` (4), `fstat` (5), `fstatat` (262), `statx` (332 - stub), `access` (21), `faccessat` (269), `readlink` (89 - stub), `getdents64` (217), `mkdir` (83), `unlink` (87), `rmdir` (84), `dup` (32), `dup2` (33).
- **Memory**: `brk` (12), `mmap` (9 - anonymous memory / lazy demand paging), `munmap` (11).
- **Time & Sync**: `clock_gettime` (228), `gettimeofday` (96), `nanosleep` (35), `futex` (202 - wait/wake).
- **System**: `uname` (63), `getrandom` (318), `getrlimit` (97), `prlimit64` (302), `set_robust_list` (273), `get_robust_list` (274).

---

## Build and Run Details

To build the static musl hello world test executable:
```bash
zig cc -target x86_64-linux-musl -static hello_musl.c -o hello_musl
```

To run the complete verification suite under QEMU:
```powershell
powershell -ExecutionPolicy Bypass -File scripts\ci-boot.ps1
```

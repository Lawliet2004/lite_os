# LiteNix Linux ABI Compatibility Matrix

This matrix documents the verification status of various Linux userspace programs and test suites under LiteNix. It is updated at each phase to honestly track what works, what fails, and what is missing.

## Phase 1 Baseline

**Build Environment:**
- clang (--target=x86_64-elf)
- ld.lld
- nasm
- xorriso
- qemu-system-x86_64
- make, python3, gcc

**Verification:** `make verify-boot` PASS (2026-06-08)

**Test Suite Results (all 19 tests PASSED):**

| # | Test | Evidence String |
|---|------|-----------------|
| 1 | Reading /hello.txt | `Test 1: PASSED` |
| 2 | VFS / stat / open / read / close | `Test 2: PASSED` |
| 3 | VFS directory listing | `Test 3: PASSED` |
| 4 | Independent file descriptor offsets | `Test 4: PASSED` |
| 5 | Virtual devices under /dev | `Test 5: PASSED` |
| 6 | Minimal procfs | `Test 6: PASSED` |
| 7 | fork and wait4 | `Test 7: PASSED` |
| 8 | getpid / getppid | `Test 8: PASSED` |
| 9 | O_TRUNC and O_APPEND | `Test 9: PASSED` |
| 10 | dup / dup2 | `Test 10: PASSED` |
| 11 | mkdir / unlink | `Test 11: PASSED` |
| 12 | clone / futex | `Test 12: PASSED` |
| 13 | lazy anonymous mmap / munmap | `Test 13: PASSED` |
| 14 | pipes / poll | `Test 14: PASSED` |
| 15 | clock_gettime / gettimeofday / sleep | `Test 15: PASSED` |
| 16 | EXT2 and tmpfs | `Test 16: PASSED` |
| 17 | socket creation / binding | `Test 17: PASSED` |
| 18 | static musl hello world | `Hello from musl!` + `Test 18: PASSED` |
| 19 | Bad executable / pointer rejection | `Test 19: PASSED` |

**No CPU exceptions** outside expected user-space negative tests.
**No kernel panics.**

For machine-readable scoreboard, see [compatibility_scoreboard.csv](./compatibility_scoreboard.csv).

## Compatibility Scoreboard

| Binary Name | Static/Dynamic | Libc Type | Required Syscalls | Pass/Fail | Last Verified Date | Serial Log Evidence |
|-------------|----------------|-----------|-------------------|-----------|--------------------|---------------------|
| **Raw Syscall Suite** (`test_all`) | Static | None (Direct) | `write`, `exit_group`, `openat`, `read`, `close`, `newfstatat`, `brk`, `mmap`, `munmap`, `getpid`, `gettid`, `uname`, `clock_gettime`, `nanosleep`, `getrandom` | **PASS** | 2026-06-08 | `RAWSYSCALL: all tests passed` |
| **Static Musl Hello** (`hello_musl`) | Static | musl | `writev`, `exit_group`, `readv` | **PASS** | 2026-06-08 | `Hello from musl!\nargc = 3\nargv[0] = /bin/hello_musl` |
| **Static BusyBox** | Static | musl | `chmod`, `fchmod`, `chown`, `statx`, `ioctl`, process groups, signals | **Fail (Target)** | 2026-06-08 | N/A (Blocked by Phase 5 syscalls) |
| **Dynamic Musl Hello** | Dynamic | musl | `mmap` (file-backed), `PT_INTERP` loader, auxiliary vectors | **Fail (Target)** | 2026-06-08 | N/A (Blocked by Phase 4 ELF improvements) |
| **BusyBox Shell** | Static | musl | `setsid`, `setpgid`, TTY line discipline, signal frame delivery | **Fail (Target)** | 2026-06-08 | N/A (Blocked by Phase 6 terminal / signals) |

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

# LiteNix Linux ABI Compatibility Matrix

This matrix documents the verification status of various Linux userspace programs and test suites under LiteNix. It is updated at each phase to honestly track what works, what fails, and what is missing.

## Phase 1 Baseline

**Verification:** `make verify-boot` PASS (2026-06-08)

## Phase 2: Real VMA and Page-Fault Infrastructure

**Verification:** `make verify-boot` PASS (2026-06-08)

**Completed Features:**
- Proper per-process Virtual Memory Area (VMA) allocation replacing the old bump allocator.
- VMA lookup, overlap detection, splitting, and merging.
- Lazy demand paging for anonymous `mmap` (pages allocated, zero-filled, and mapped on-demand).
- Validation-time on-demand page mapping in `vmm_validate_user_ptr_ex` to support syscall copy operations.
- VMA permission checks during page faults. Userspace memory violations safely trigger SIGSEGV and clean process termination.

**Verification Suite Additions (in `/tests/test_all`):**
- Lazy page fault allocation validation.
- Invalid size checks.
- Accessing unmapped memory after `munmap` (triggers child SIGSEGV verification).
- Writing to a page protected to read-only via `mprotect` (triggers child SIGSEGV verification).
- Accessing memory mapped with `PROT_NONE` (triggers child SIGSEGV verification).
- Overlapping mmap replacement using `MAP_FIXED` (triggers replacement and fresh zero-fill).

**Test Suite Results (all 19 tests PASSED):**

**Verification:** `powershell -ExecutionPolicy Bypass -File scripts\ci-boot.ps1` PASS (2026-06-08)

**Completed Features:**
- Proper per-process Virtual Memory Area (VMA) allocation replacing the old bump allocator.
- VMA lookup, overlap detection, splitting, and merging.
- Lazy demand paging for anonymous `mmap` (pages allocated, zero-filled, and mapped on-demand).
- Validation-time on-demand page mapping in `vmm_validate_user_ptr_ex` to support syscall copy operations.
- VMA permission checks during page faults. Userspace memory violations safely trigger SIGSEGV and clean process termination.

**Verification Suite Additions (in `/tests/test_all`):**
- Lazy page fault allocation validation.
- Invalid size checks.
- Accessing unmapped memory after `munmap` (triggers child SIGSEGV verification).
- Writing to a page protected to read-only via `mprotect` (triggers child SIGSEGV verification).
- Accessing memory mapped with `PROT_NONE` (triggers child SIGSEGV verification).
- Overlapping mmap replacement using `MAP_FIXED` (triggers replacement and fresh zero-fill).

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
| 20 | File-backed mmap read / offset / beyond-EOF / private-write / bad-fd / misaligned-offset | `Test 20: PASSED` |

**No CPU exceptions** outside expected user-space negative tests.
**No kernel panics.**

For machine-readable scoreboard, see [compatibility_scoreboard.csv](./compatibility_scoreboard.csv).

## Compatibility Scoreboard

| Binary Name | Static/Dynamic | Libc Type | Required Syscalls | Pass/Fail | Last Verified Date | Serial Log Evidence |
|-------------|----------------|-----------|-------------------|-----------|--------------------|---------------------|
| **Raw Syscall Suite** (`test_all`) | Static | None (Direct) | `write`, `exit_group`, `openat`, `read`, `close`, `newfstatat`, `brk`, `mmap`, `munmap`, `getpid`, `gettid`, `uname`, `clock_gettime`, `nanosleep`, `getrandom` | **PASS** | 2026-06-08 | `RAWSYSCALL: all tests passed` |
| **Static Musl Hello** (`hello_musl`) | Static | musl | `writev`, `exit_group`, `readv` | **PASS** | 2026-06-08 | `Hello from musl!\nargc = 3\nargv[0] = /bin/hello_musl` |
| **Static BusyBox** | Static | musl | `chmod`, `fchmod`, `chown`, `fchown`, `lchown`, `umask`, `mkdirat`, `fchownat`, `unlinkat`, `renameat`, `symlinkat`, `readlinkat`, `fchmodat`, `dup3`, `renameat2`, `symlink`, `readlink` | **PASS** | 2026-06-09 | `Test 23: PASSED` printed in serial log |
| **Dynamic Binary Test** | Dynamic | N/A | `mmap` (file-backed), `PT_INTERP` loader, auxiliary vectors | **PASS** | 2026-06-09 | `Dynamic kernel loader path works!` in serial log |
| **Dynamic Musl Hello** | Dynamic | musl | Full musl dynamic linker, shared libs, relocation | **PASS** | 2026-06-09 | `Hello from dynamic musl!` printed in serial log |
| **BusyBox Shell** | Static | musl | `setsid`, `setpgid`, TTY line discipline, signal frame delivery | **Fail (Target)** | 2026-06-08 | N/A (Blocked by Phase 6 terminal / signals) |

---

## Syscall Support Summary for Static Binaries

The following system calls have been verified and validated against bad user pointer inputs:

- **Process / Thread**: `exit` (60), `exit_group` (231), `getpid` (39), `gettid` (186), `getppid` (110), `fork` (57), `wait4` (61), `clone` (56 - thread setup), `set_tid_address` (218).
- **Filesystem**: `read` (0), `write` (1), `readv` (19), `writev` (20), `open` (2), `openat` (257), `close` (3), `lseek` (8), `stat` (4), `fstat` (5), `lstat` (6), `fstatat` (262), `statx` (332 - stub), `access` (21), `faccessat` (269), `readlink` (89), `readlinkat` (267), `getdents64` (217), `mkdir` (83), `mkdirat` (258), `unlink` (87), `unlinkat` (263), `rmdir` (84), `dup` (32), `dup2` (33), `dup3` (292), `chmod` (90), `fchmod` (91), `fchmodat` (268), `chown` (92), `fchown` (93), `lchown` (94), `fchownat` (260), `umask` (95), `renameat` (264), `renameat2` (316), `symlink` (88), `symlinkat` (266).
- **Memory**: `brk` (12), `mmap` (9 - anonymous memory / lazy demand paging / file-backed MAP_PRIVATE), `munmap` (11).
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

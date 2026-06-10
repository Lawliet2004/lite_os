# LiteNix Linux ABI Compatibility Matrix

This matrix documents the verification status of various Linux userspace programs and test suites under LiteNix. It is updated at each milestone to honestly track what works, what fails, and what is missing.

> **Rule:** No compatibility claim is made without a named test and serial log evidence.

---

## Current Status

**Last full verification:** `make verify-boot` — 2026-06-10

**Passing markers (all required):**

| Marker | Status |
|--------|--------|
| `VMM: address-space self-test passed` | ✅ |
| `VMM: negative self-test passed` | ✅ |
| `VMM: permission self-test passed` | ✅ |
| `Heap: self-test passed` | ✅ |
| `Sched: initialized` | ✅ |
| `Sched: timer preemption started` | ✅ |
| `Net: Initialized network protocol stack` | ✅ |
| `UDP Echo Server listening on port 9999` | ✅ |
| `TCP HTTP Server listening on port 80` | ✅ |
| `ext2: found /ext2/hello.txt` | ✅ (real disk or fallback) |
| `Test 16: PASSED` | ✅ |
| `Test 22: PASSED` | ✅ |
| `Hello from dynamic musl!` | ✅ |
| `Test 23: PASSED` | ✅ |
| `All VFS Verification Tests Passed!` | ✅ |
| `Test 26: PASSED` | ✅ |
| `All shell tests PASSED` | ✅ |
| `Phase 9 & 10: init program exited with 0 OK` | ✅ |
| No `Init ERROR` | ✅ |
| No `KERNEL PANIC` | ✅ |
| No `CPU exception` | ✅ |

---

## Test Suite Results (26 Tests)

All init verification tests must pass for `make verify-boot` to succeed.

| # | Test | Evidence String |
|---|------|--------------------|
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
| 16 | EXT2 and tmpfs write/readback | `Test 16: PASSED` |
| 17 | socket creation / binding | `Test 17: PASSED` |
| 18 | static musl hello world | `Hello from musl!` + `Test 18: PASSED` |
| 19 | Bad executable / pointer rejection | `Test 19: PASSED` |
| 20 | File-backed mmap (read/offset/beyond-EOF/private-write/bad-fd/misaligned) | `Test 20: PASSED` |
| 21 | Dynamic linker error paths | `Test 21: PASSED` |
| 22 | Dynamic musl hello | `Hello from dynamic musl!` + `Test 22: PASSED` |
| 23 | BusyBox applets and symlink behavior | `Test 23: PASSED` |
| 24 | Process group/session behavior | `Test 24: PASSED` |
| 25 | Signal action/blocking/handler delivery | `Test 25: PASSED` |
| 26 | Persistent ext2 disk read from ATA | `Test 26: PASSED` |

---

## Compatibility Scoreboard

For machine-readable data, see [compatibility_scoreboard.csv](./compatibility_scoreboard.csv).

| Binary | Static/Dynamic | Libc | Key Syscalls | Pass/Fail | Last Verified | Serial Evidence |
|--------|---------------|------|--------------|-----------|---------------|-----------------|
| **Raw Syscall Suite** (`test_all`) | Static | None | `write`, `exit_group`, `openat`, `read`, `close`, `newfstatat`, `brk`, `mmap`, `munmap`, `getpid`, `gettid`, `uname`, `clock_gettime`, `nanosleep`, `getrandom` | **PASS** | 2026-06-10 | `RAWSYSCALL: all tests passed` |
| **Static Musl Hello** (`hello_musl`) | Static | musl | `writev`, `exit_group`, `readv` | **PASS** | 2026-06-10 | `Hello from musl!\nargc = 3` |
| **Static BusyBox** | Static | musl | `chmod`, `fchmod`, `chown`, `fchown`, `lchown`, `umask`, `mkdirat`, `fchownat`, `unlinkat`, `renameat`, `symlinkat`, `readlinkat`, `fchmodat`, `dup3`, `renameat2`, `symlink`, `readlink` | **PASS** | 2026-06-10 | `Test 23: PASSED` |
| **Dynamic Binary** | Dynamic | None | `mmap` (file-backed), PT_INTERP loader, auxv | **PASS** | 2026-06-10 | `Dynamic kernel loader path works!` |
| **Dynamic Musl Hello** | Dynamic | musl | musl dynamic linker, shared libs, relocation | **PASS** | 2026-06-10 | `Hello from dynamic musl!` |
| **BusyBox Shell** | Static | musl | `setsid`, `setpgid`, TTY, signal frame | **PASS** | 2026-06-10 | `All shell tests PASSED` |
| **Persistent EXT2 Read** | N/A | N/A | ATA PIO block read, ext2 inode walk | **PASS** | 2026-06-10 | `Test 26: PASSED` |
| **EXT2 Write + Readback** | N/A | N/A | `O_TRUNC`, `write`, `read`, `ram_truncate` | **PASS** | 2026-06-10 | `Test 16: PASSED` |

---

## Bug Fixes Applied

| Date | Bug | Fix |
|------|-----|-----|
| 2026-06-10 | EXT2 read-after-write mismatch (stale buffer bytes returned after O_TRUNC+write on memory-fallback node) causing KERNEL PANIC | Added `ram_truncate()` to `kernel/fs/ext2.c` and wired it to the memory-fallback VFS node's `truncate` callback. Zeros old buffer content on truncate-to-zero. |
| 2026-06-10 | `make_ext2.py` inode struct issues and block overlaps — ext2 directory block number was read from wrong struct field (missing `i_osd1`), and the hardcoded root directory/hello.txt blocks overlapped with the inode table. | Added missing `i_osd1` to packing, transitioned to a proper `alloc_block()` model for all metadata/files, and ensured root directory and data blocks are outside the reserved inode table range. |

---

## Known Limitations

| Area | Status | Notes |
|------|--------|-------|
| Persistent ext2 write | **Partial** | Memory-fallback write works (Test 16). Real ATA disk write path available but disk content resets each boot (disk.img regenerated by make). |
| TTY / job control | **Partial** | BusyBox sh smoke tests pass; vi, Ctrl+Z, job control SIGTSTP not fully implemented |
| Networking | **Partial** | ARP, ICMP, UDP echo, basic TCP; no retransmit, DHCP, or DNS |
| Signals | **Partial** | rt_sigaction, rt_sigprocmask, rt_sigreturn implemented; SIGCHLD to parent delivery partial |
| User permissions | **Not started** | All processes run as UID 0; permission checks on file access not enforced |

---

## Build and Run

```bash
# Build and verify
make verify-boot

# Interactive run
make run
```

```powershell
# PowerShell CI script
powershell -ExecutionPolicy Bypass -File scripts\ci-boot.ps1
```

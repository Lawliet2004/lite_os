# LiteNix OS

LiteNix OS is a lightweight, Linux-like operating-system project for x86_64.
The first target is a small kernel that boots in QEMU through Limine and prints
diagnostics over both VGA text output and COM1 serial.

## Current Scope

This repository currently implements the early kernel foundation plus partial
Linux-like userspace support. Phases 0 through 13 are substantially present,
with partial work beyond that for process control, memory mapping, pipes, time,
storage, and networking:

- repository structure
- Limine boot configuration
- minimal x86_64 kernel entry
- serial COM1 output
- VGA text output
- small `printk` formatter
- `panic()` and `KASSERT()`
- GDT and TSS setup
- IDT entries for CPU exceptions 0-31
- exception diagnostics for vector, error code, registers, and page-fault CR2
- legacy PIC remapping
- PIT timer interrupts with a boot-time tick smoke test
- minimal keyboard IRQ acknowledgement
- bitmap physical memory manager with 4 KiB pages
- PMM self-test using 10,000 page allocations
- kernel page-table map/unmap/protect/translate helpers
- VMM self-test using a PMM-backed virtual mapping:
  - write/readback, permission change, unmap, translation-fail checks
  - separate user address-space creation with kernel-mapping copy
  - CR3 switch into user address space to verify kernel continuity
  - user/kernel address validation checks
  - copy between address spaces (structural, SMAP enforcement in Phase 9)
  - address-space destruction with recursive page-table page cleanup
  - PMM page-leak detection after destruction
- guard page support (`VMM_GUARD_PAGE_SIZE` convention)
- segregated free-list kernel heap allocator
- `kmalloc`, `kzalloc`, `kfree`, `krealloc`, `kmalloc_aligned`
- heap self-test: small/medium/large allocations, krealloc grow, aligned alloc,
  500-block allocate/free stress cycle with leak check
- poison-pattern debug mode with double-free detection
- process and task structures with separate PID and TID fields
- parent/child process links for kernel-created tasks
- kernel thread creation, exit, zombie state, wait, and reap
- priority-based round-robin scheduler with nice -20..+19 (40 levels)
- time-slice proportional to priority (high-priority tasks get longer slices)
- timed sleep via `task_sleep_ticks()` with PIT-driven wakeup
- dedicated idle task with lowest priority as CPU fallback
- starvation prevention with periodic priority boosting
- per-task CPU time accounting (`total_ticks`)
- Phase 7 boot self-test: fair-share, priority ordering, and sleep/wake tests
- Linux x86_64 `SYSCALL`/`SYSRET` ABI via EFER/STAR/LSTAR/SFMASK MSR setup
- GDT expanded with ring-3 user code and data segments (DPL=3)
- assembly syscall entry stub saving all GP registers as `struct syscall_frame`
- 256-entry dispatch table; unknown calls return `-ENOSYS`
- negative errno convention (`-EFAULT`, `-EBADF`, `-ENOSYS`, …)
- `sys_write` (fd 1/2 → serial), `sys_exit`, `sys_exit_group` handlers
- `copy_from_user` / `copy_to_user` with address-range and overflow validation
- optional `SYSCALL_TRACE` compile flag for per-call tracing
- Phase 8 boot self-test: `-ENOSYS`, `-EFAULT`, `-EBADF`, and `uaccess_ok` tests
- ring-3 userspace transition and ELF loading for static test programs
- initramfs-backed `/bin/init` and `/bin/sh`
- VFS, basic file descriptors, devfs nodes, and procfs nodes
- fork, execve, wait4, clone/futex subset, brk, lazy anonymous mmap, pipes, poll,
  time syscalls, ext2 test storage, and socket API subset
- additional Linux compatibility syscalls such as `openat`, `newfstatat`,
  `gettid`, `uname`, `getrandom`, `statx`, `access`/`faccessat`, resource-limit
  queries, and robust-list registration stubs
- QEMU and ISO helper scripts

It does not yet implement broad Linux application compatibility. Dynamic
linking, file-backed `mmap`, complete signals, robust futex recovery, complete
TTY behavior, package management, browser-class applications, and a proven
VirtualBox workflow are still future work.

## Linux Compatibility Policy

LiteNix must not claim to run all Linux applications. Compatibility is measured
level by level, with each level backed by explicit test programs and serial-log
evidence. See [docs/linux-compat.md](docs/linux-compat.md) and
[docs/syscall-roadmap.md](docs/syscall-roadmap.md).

The full project roadmap is tracked in [docs/roadmap.md](docs/roadmap.md).
The staged Linux-like OS plan is tracked in
[docs/linux-os-plan.md](docs/linux-os-plan.md).

## Prerequisites

Install:

- `make`
- `clang` with an x86_64 freestanding target, or `x86_64-elf-gcc`
- `nasm`
- `xorriso`
- QEMU (`qemu-system-x86_64`)
- Limine files available under `toolchain/limine/`

Expected Limine files:

- `toolchain/limine/limine-bios.sys`
- `toolchain/limine/limine-bios-cd.bin`
- `toolchain/limine/limine`

Follow [toolchain/README.md](toolchain/README.md) to install Limine locally.
See [docs/external-code.md](docs/external-code.md) for external artifact
tracking.

## Build

```sh
make
```

## Build ISO

```sh
make iso
```

## Run In QEMU

```sh
make run
```

The serial log is written to:

```text
build/serial.log
```

## Verify Boot

```sh
make verify-boot
```

This boots QEMU with a timeout and checks the serial log for the VMM, heap, and
Phase 7 scheduler self-test success markers. It also fails if the log contains a
CPU exception or kernel panic.

Expected output:

```text
LiteNix kernel booted
Architecture: x86_64
Serial: initialized
Memory map: detected
Kernel status: OK
```

Phase 2/3 also prints:

```text
GDT: initialized
IDT: initialized
PMM: initialized
PMM: total=... KiB usable=... KiB free=... KiB reserved=... KiB
PMM: bitmap=... bytes at phys=...
PMM: self-test passed (10000 pages)
VMM: initialized
VMM: self-test passed
PIC: remapped
PIT: initialized at 100 Hz
Timer: ticks observed (...)
```

Phase 7 scheduler output:

```text
Sched: initialized with 1 tasks (Phase 7)
Sched: idle task registered (priority=19)
Sched: timer preemption started
phase7_equal_a: started (prio=0)
phase7_equal_b: started (prio=0)
phase7_equal_a: done (ticks=...)
phase7_equal_b: done (ticks=...)
Sched: fair-share test passed
phase7_hipri: started (prio=-10)
phase7_lopri: started (prio=10)
phase7_hipri: done (ticks=...)
phase7_lopri: done (ticks=...)
Sched: priority test passed
phase7_sleeper: sleeping 50 ticks
phase7_sleeper: woke after ... ticks
Sched: sleep test passed
Sched: Phase 7 scheduler self-test passed
```

## Exception Smoke Tests

Build with one controlled exception test enabled:

```sh
make clean
make TEST=divide run
```

or:

```sh
make clean
make TEST=pagefault run
```

The kernel should print CPU exception diagnostics and then panic.

## Next Step

Stabilize the current partial userspace stack, then move through the
compatibility milestones in [docs/linux-os-plan.md](docs/linux-os-plan.md).

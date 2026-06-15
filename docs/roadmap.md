# LiteNix Roadmap

LiteNix is built in measurable phases. A phase is only considered complete when
its implementation, documentation, and boot or test evidence are present.

## Phase 0: Project Bootstrap

- repository structure
- buildable empty kernel
- bootable ISO
- QEMU run script
- VirtualBox instructions
- serial output

Acceptance: QEMU boots and serial output reports the kernel banner, CPU
architecture, memory map summary, bootloader name, and success marker.

## Phase 1: Boot, Logging, And Panic System

- early boot entry and stack setup
- VGA or framebuffer text output
- COM1 serial logging
- `printk`
- `panic`
- assertions
- early halt loop

Acceptance: the kernel boots, can intentionally panic, and serial captures the
complete diagnostic output.

## Phase 2: CPU Setup, GDT, IDT, Interrupts

- GDT
- TSS
- IDT
- exception handlers
- IRQ handlers
- page fault diagnostics
- timer interrupt
- optional keyboard interrupt

Acceptance: divide-by-zero and invalid memory access are reported, and the
timer tick counter advances.

## Phase 3: Physical Memory Manager

- bootloader memory-map parsing
- 4 KiB page bitmap
- page allocation and free
- contiguous page allocation and free
- memory statistics
- debug invalid-free and double-free checks

Acceptance: 10,000 pages allocate and free with no leak, invalid frees are
detected, and memory usage prints at boot.

## Phase 4: Virtual Memory Manager

- x86_64 4-level paging
- kernel address space
- user address spaces
- page-map creation and destruction
- map, unmap, protect, and translate
- user pointer validation
- guard pages

Acceptance: user and kernel memory are separated, kernel addresses are rejected
as user pointers, null faults work, and guard pages trap overflow.

## Phase 5: Kernel Heap And Object Allocation

- early bump allocator
- `kmalloc` and `kfree`
- aligned allocation
- optional slab allocator
- allocation statistics
- debug poisoning and double-free detection

Acceptance: heap stress tests pass with no leaks, and heap usage is reported.

## Phase 6: Process And Thread Model

- process, thread, task, PID, and TID model
- parent and child relationships
- kernel threads
- user tasks
- task creation and exit
- zombie state and wait behavior

Acceptance: two kernel threads context switch, timer preemption works, and an
exited task remains zombie until reaped.

## Phase 7: Linux-Like Scheduler

- round-robin scheduler with priority
- sleep and wakeup
- idle task
- fair scheduling later
- realtime classes later
- EEVDF-inspired behavior later

Acceptance: CPU-bound tasks share CPU fairly, sleepers wake correctly,
interactive tasks respond, and starvation is avoided.

## Phase 8: Syscall Infrastructure

- Linux x86_64 syscall ABI
- syscall entry and register save
- syscall table
- `-ENOSYS` for unknown calls
- negative errno convention
- tracing mode
- user pointer copying

Acceptance: a user program can call `write` and `exit`, unknown syscalls return
`-ENOSYS`, and bad user pointers return `-EFAULT`.

## Phase 9: User Mode

- ring 3 transition
- user code and data segments
- user stack
- initial user process
- return to user mode
- kill process on userspace fault

Acceptance: a user program prints through a syscall, cannot read kernel memory,
cannot execute privileged instructions, and cannot crash the kernel.

## Phase 10: ELF Loader

- ELF64 header parsing
- PT_LOAD segment loading
- segment permission handling
- BSS zeroing
- entry point setup
- initial user stack
- argc, argv, and envp
- auxv later
- PT_INTERP detection and interpreter loading (tested with fake interpreter)

Acceptance: static hello world loads, invalid ELF files are rejected safely, and
failed exec does not leak kernel memory.

## Phase 11: Virtual Filesystem

- VFS core objects
- initramfs root
- basic path resolution
- file descriptor table
- read, write, open, close, seek, and stat paths

Acceptance: initramfs contains `/bin/init` and `/bin/sh`, shell can list files,
`cat` can read files, and multiple file descriptors work.

## Phase 12: Device Model

- `/dev/null`
- `/dev/zero`
- `/dev/full`
- `/dev/random` stub
- `/dev/urandom`
- `/dev/console`
- `/dev/tty`
- `/dev/kmsg`
- basic TTY behavior

Acceptance: console output works, `/dev/zero` reads zeros, `/dev/null`
discards writes, and shell input works.

## Phase 13: Procfs And Sysfs Minimal Compatibility

- `/proc`
- `/proc/self`
- `/proc/meminfo`
- `/proc/cpuinfo`
- `/proc/uptime`
- `/proc/stat`
- `/proc/version`
- `/proc/<pid>/status`
- minimal `/sys` only when needed

Acceptance: `/proc/meminfo` and `/proc/cpuinfo` work, and process inspection
can be added incrementally.

## Phase 14: Memory Efficiency Features

- demand allocation
- lazy stack growth later
- copy-on-write fork later
- shared zero page later
- reclaimable caches
- compact kernel objects
- configurable limits
- memory metrics

Acceptance: the OS boots and runs a shell under 64 MB RAM, and process exit
returns memory.

## Phase 15: Fork, Exec, Wait, Exit

- fork or clone-like primitive
- `execve`
- `wait4`
- `exit`
- `exit_group`
- zombie handling
- orphan reparenting
- fd inheritance
- copy-on-write later

Acceptance: the shell starts a child, waits for it, receives its exit code, and
reaps zombies.

## Phase 16: Signals

- pending signal mask
- default actions
- SIGKILL, SIGTERM, SIGSEGV, SIGCHLD, SIGINT
- `rt_sigaction` stub
- `rt_sigprocmask`
- signal delivery before userspace return
- handlers implemented and verified

Acceptance: invalid memory access sends SIGSEGV, parent receives SIGCHLD, and SIGINT works for foreground processes. Handlers verified in `init.c` Test 25.

## Phase 17: Futexes And Threading

- FUTEX_WAIT
- FUTEX_WAKE
- FUTEX_PRIVATE_FLAG
- clone subset
- TLS through `arch_prctl`
- `set_tid_address`
- clear-child-tid behavior
- shared address space between threads

Acceptance: a simple pthread program runs, mutexes work, and multithreaded
process exit is clean.

## Phase 18: Mmap, Brk, And User Memory Compatibility

- `brk`
- anonymous private `mmap`
- `munmap`
- `mprotect`
- careful MAP_FIXED behavior
- VMA tracking
- demand paging
- file-backed mappings later

Acceptance: `malloc` works in a static musl program, anonymous `mmap` works,
invalid memory faults, and sparse mappings are lazy.

**Phase 18A: File-Backed Mmap (Milestone 2)**

- file-backed `mmap` with MAP_PRIVATE
- COW page fault loading from file inode
- offset alignment validation (page-aligned required)
- bytes beyond EOF are zero-filled
- MAP_PRIVATE writes do not modify underlying file
- mmap with invalid fd returns EBADF
- mmap with misaligned offset returns EINVAL

Acceptance: file-backed mmap tests pass (read, offset, beyond EOF, COW write,
bad fd, misaligned offset). Serial evidence: `FILE_MMAP: all tests passed`.

## Phase 19: Linux Application Compatibility Roadmap

- define Linux compatibility levels 0 through 10
- maintain a compatibility test matrix
- record missing syscalls
- implement only what tested programs require next

Acceptance: compatibility claims are backed by named binaries and serial-log
evidence.

## Phase 20: Shell And Init System

- `/bin/init`
- simple shell
- command execution
- argv parsing
- environment variables
- builtins such as `cd`, `pwd`, `echo`, `cat`, and `ls`
- init mounts filesystems and starts shell

Acceptance: the OS boots into a shell, runs `/bin/hello` and `/bin/ls`, and recovers after a child crash. Verified by BusyBox shell integration.

## Phase 21: Pipes, Poll, Select, Epoll

- `pipe`
- `pipe2`
- blocking read and write
- nonblocking mode
- `poll`
- `select` later
- `epoll` later

Acceptance: `echo hello | cat` works, producer and consumer tests pass, and
poll detects readable pipes without lost wakeups.

## Phase 22: Timekeeping

- monotonic time
- realtime clock
- `clock_gettime`
- `gettimeofday`
- `nanosleep`
- per-task sleep queues
- blocking syscall timeouts later

Acceptance: `sleep 1` works, `clock_gettime` increases, and scheduler
sleep/wakeup works.

## Phase 23: Storage And Filesystems

- initramfs first
- tmpfs next
- ext2 read-only
- ext2 read/write
- virtio-blk (in progress)
- simple block cache (partial)
- page cache later

Acceptance: initramfs mounts as root, tmpfs mounts on `/tmp`, ext2 images can be read, and simple ext2 writes work (Verified by `init.c` Test 26/27/28).

## Phase 24: Networking Later

- virtio-net
- Ethernet
- ARP
- IPv4
- ICMP
- UDP
- TCP with retransmission timers and proper FIN handshake state machine
- sockets API

Acceptance: guest can ping host, UDP echo works, TCP connects, and a simple
HTTP server can run. Verified with robust TCP state machine transitions (SYN, ESTABLISHED, FIN_WAIT, TIME_WAIT, CLOSE_WAIT) and TCP segment retransmission timers with exponential backoff.

## Phase 25: VirtualBox Support

- boot ISO in VirtualBox
- generic PC hardware path
- documented VM settings
- serial logging
- keyboard support

Acceptance: ISO boots, the banner prints, keyboard works, shell works, and
there is no triple fault or reboot loop.

## Phase 26: Debugging System

- serial logging
- kernel log ring buffer
- `/dev/kmsg`
- panic dump
- stack traces later
- syscall tracing
- scheduler tracing
- memory tracing
- QEMU and GDB workflow

Acceptance: crashes are diagnosable, failed syscalls show useful context, and
memory leaks can be traced in debug builds.

## Phase 27: Security Rules

- hostile-userspace model
- syscall argument validation
- safe user copy helpers
- overflow checks
- fd bounds checks
- permission checks
- no kernel memory leaks to userspace
- user and kernel page separation
- W^X where practical

Acceptance: bad user pointers return `-EFAULT`, huge sizes do not overflow,
userspace cannot read kernel memory, and invalid syscalls cannot crash the
kernel.

## Phase 28: Performance And RAM Efficiency

- boot time metrics
- kernel image size metrics
- boot memory usage
- shell memory usage
- syscall overhead
- context-switch cost
- page-fault cost
- scheduler latency
- fragmentation estimates

Acceptance: performance is measured before optimization, hot paths avoid
allocation, and RAM budgets are tracked.

## Phase 29: Testing Strategy

- kernel unit tests
- kernel integration tests
- userspace tests
- compatibility tests
- QEMU boot CI
- serial success marker checks
- panic and leak failure checks

Acceptance: every layer has tests, compatibility is scoreboard-driven, and CI
fails on panic or debug memory leaks.

## Phase 30: AI Development Rules

- work subsystem by subsystem
- every change must compile
- every change must boot or explain why not
- review invariants before coding
- include tests with subsystems
- review memory safety
- verify Linux ABI behavior
- no fake compatibility claims

Acceptance: every implementation step reports files changed, implementation,
invariants, build steps, test steps, expected serial output, known limitations,
and next step.

## Phase 31: SMP Foundation (BSP-only)

Partially completed in the current development cycle. Phase 1
established the per-CPU infrastructure without bringing up APs yet.
Phase 5 added per-CPU TLB-shootdown queues so cross-CPU vmm_unmap
fires invlpg-by-address instead of a full TLB flush. Phase 6 bounded
the LAPIC IPI wait and wired the scheduler's reschedule-decision
path to `smp_send_ipi_to_cpu()`, unblocking `-smp 4` boots. The
per-CPU runqueue refactor (Phase 4) added the fields to
`struct cpu_data` but the scheduler still uses global state — a
full migration showed addc=3/add=3 (all tasks correctly added to
the per-CPU runqueue) but the BSP then failed to make progress
through the Phase 9 & 10 test suite. The hang couldn't be isolated
in the BSP-only test path; needs live APs to bisect. Reverted to
globals so the kernel boots.

- [x] `kernel/include/arch/x86_64/smp.h` — per-CPU API, MSR numbers,
  LAPIC register offsets, ICR delivery-mode encodings, compile-time
  offset checks for the per-CPU struct, TLB-shootdown queue types
  and accessors (Phase 5), `reschedule_self_test` decl (Phase 6),
  per-CPU runqueue fields (`runqueue_head`, `runqueue_tail`,
  `task_count`, `idle_task`, Phase 4 partial).
- [x] `kernel/arch/x86_64/smp.c` — APIC detection (CPUID.1:EDX bit 9),
  LAPIC base from IA32_APIC_BASE, `smp_init()` populates the per-CPU
  array and installs the BSP's GS base, `smp_ap_entry()` C-side
  trampoline landing, IPI send helpers (with 10 000-spin bounded
  wait on ICR `delivery_status`, Phase 6), per-CPU TLB-shootdown
  queue publish/drain and `tlb_shootdown_self_test()` (Phase 5),
  `reschedule_self_test()` (Phase 6).
- [x] `kernel/arch/x86_64/syscall/syscall_entry.c` — migrated
  `bsp_cpu_context` to `g_cpu_data[0]`; `syscall_set_kernel_rsp`
  updates the per-CPU kernel stack slot.
- [x] `kernel/arch/x86_64/syscall/syscall_stub.S` — syscall entry
  reads kernel stack from `gs:8` (was `gs:0`) and saves user RSP to
  `gs:16` (was `gs:8`) to match the new `cpu_data` layout.
- [x] `kernel/arch/x86_64/memory/vmm.c` — `vmm_unmap` publishes the
  vaddr to every other CPU's TLB queue before broadcasting
  `IPI_VECTOR_TLB_SHOOTDOWN` (Phase 5).
- [x] `kernel/arch/x86_64/interrupt/isr.S` + `idt.c` — three IPI
  stubs (`isr_ipi0/1/2`) and IDT gates for vectors 0x40–0x42.
- [x] `kernel/sched/scheduler.c` — `sched_tick()` calls
  `smp_send_ipi_to_cpu(smp_current_cpu_id(), IPI_VECTOR_RESCHEDULE)`
  after the per-CPU `need_resched` is set when `g_cpu_count > 1`
  (Phase 6 + Phase 6 followup). The `need_resched` flag is per-CPU
  (`g_cpu_data[cpu].need_resched`), zeroed in `smp_init` and consumed
  by the idle loop and `schedule()`. The reschedule IPI handler
  (`ipi_dispatch` in `smp.c`) sets the target CPU's per-CPU flag, so
  when APs come up, the target's next `schedule()` will actually
  re-evaluate. The runqueue is still global; the per-CPU fields in
  `struct cpu_data` are zeroed in `smp_init` and ready for the
  migration.
- [x] `kernel/core/kernel.c` — calls `smp_init()`,
  `tlb_shootdown_self_test()`, `reschedule_self_test()`, and
  `panic_broadcast_self_test()` (Phase 32 followup) after the
  scheduler init. With `-smp 1` and `-smp 4` both, the kernel
  reaches userspace and runs the test suite (Phase 9 & 10, Tests
  1–22) without regression.
- [ ] `kernel/arch/x86_64/ap_trampoline.S` — 16-bit→64-bit AP startup
  code (deferred to Phase 32). Blocked on QEMU 8.2.2 leaving the
  AP's LAPIC SVR at 0 in the "wait for SIPI" code path. GDB stub
  workaround attempted but blocked: no `gdb`/`gdb-multiarch` is
  installed in MSYS2 (`pacman` not on PATH either), so we can't
  connect to QEMU's `-gdb` stub to manually set the AP's LAPIC
  SVR.

Acceptance: `qemu -smp 1` and `qemu -smp 4` both boot to
"Sched: reschedule IPI self-test passed" and run the full init
test suite (Phase 9 & 10: `test_read_kernel terminated OK`,
`test_privileged terminated OK`, `init program exited with 0 OK`,
Tests 1–22: PASSED). With `-smp 4` the BSP detects the LAPIC,
runs both self-tests, and reaches the AP-bring-up phase; APs
remain parked at the QEMU "wait for SIPI" stall (`LAPIC SVR=0`);
the bounded wait in `lapic_send_ipi()` ensures the BSP doesn't
hang on the broadcast to those APs.
in `lapic_send_ipi()` ensures the BSP doesn't hang on the broadcast
to those APs.

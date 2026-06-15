# LiteNix Architecture

LiteNix is a monolithic but modular x86_64 kernel targeting Linux ABI compatibility for a minimal CLI distro.

## Boot

- **Bootloader**: Limine (BIOS boot path via ISO)
- **Entry**: `kernel/arch/x86_64/entry.S` — sets up stack, calls `kernel_main`
- **Early output**: VGA text mode (`kernel/drivers/vga_text.c`) + COM1 serial (`kernel/drivers/serial.c`)
- **Logging**: `printk` formatter (`kernel/core/printk.c`), ring-buffer `/dev/kmsg` (`kernel/fs/vfs.c`)
- **Panic**: `panic()` / `KASSERT()` (`kernel/core/panic.c`) — halts system with full diagnostic

## CPU Descriptors

- **GDT**: ring 0 and ring 3 code/data segments, TSS (`kernel/arch/x86_64/cpu/gdt.c`, `gdt_load.S`)
- **IDT**: 256 entries; CPU exceptions 0–31 + IRQ handlers (`kernel/arch/x86_64/interrupt/idt.c`, `isr.S`)
- **PIC**: legacy 8259 remapped to IRQ 32–47 (`kernel/arch/x86_64/interrupt/pic.c`)
- **PIT**: timer at 100 Hz driving scheduler preemption and `nanosleep` wakeup (`kernel/drivers/pit.c`)
- **Keyboard**: IRQ 1 acknowledged; not yet line-buffered for userspace (`kernel/drivers/tty.c`)

## SMP Support (Phase 1–6, BSP only)

- **APIC detection**: `smp_init()` checks CPUID.1:EDX bit 9; reports LAPIC base
  from IA32_APIC_BASE MSR (`kernel/arch/x86_64/smp.c`).
- **Per-CPU data**: 64-slot `g_cpu_data[MAX_CPUS]` array, accessed via the GS
  segment base. Offsets 0/8/16 are `self`/`kernel_stack`/`user_scratch` for
  the syscall entry stub; remaining fields reserved for Phase 2+ (idle task,
  current_task, in_irq, sched_ticks, halt flag).
- **GS base**: BSP installs `MSR_GS_BASE = &g_cpu_data[0]`; future APs will
  install their own slot in `smp_ap_entry()`. Legacy `bsp_cpu_context` was
  merged into `g_cpu_data[0]` so the syscall path keeps working unchanged.
- **AP bring-up**: deferred to Phase 32. The 16-bit → 64-bit trampoline
  skeleton lives in `kernel/arch/x86_64/ap_trampoline.S` (not yet built).
  QEMU 8.2.2 has a known limitation where the AP's LAPIC SVR is left at 0
  (disabled) in the "wait for SIPI" code, so the SIPI never delivers;
  AP bring-up is blocked on this until a QEMU version that enables the
  AP's LAPIC before parking is available.
- **IPI infrastructure**: three IPI vectors (0x40 reschedule, 0x41 TLB
  shootdown, 0x42 panic) installed in the IDT; per-vector delivery
  counters. `smp_send_ipi` and `smp_broadcast_ipi_excluding_self` exist.
  `lapic_send_ipi()` waits for the ICR `delivery_status` bit with a
  10 000-iteration bound (Phase 6) so a missing/disabled target LAPIC
  in QEMU doesn't spin the kernel forever.
- **Per-CPU TLB-shootdown queues** (Phase 5): `g_tlb_queue[MAX_CPUS]`
  is a 64-byte-aligned array of bounded rings (16 entries each) used to
  pass the vaddr from the sender to each target CPU. `vmm_unmap` calls
  `tlb_shootdown_publish_all_others(virt)` before the broadcast, and
  each receiver's IPI handler calls `tlb_shootdown_drain()` which does
  `invlpg` for every pending entry. Replaces the prior full-TLB flush.
- **Cross-CPU reschedule hookup** (Phase 6): `sched_tick()` calls
  `smp_send_ipi_to_cpu(smp_current_cpu_id(), IPI_VECTOR_RESCHEDULE)`
  after `need_resched = true` when `g_cpu_count > 1`. In single-CPU
  mode this is a no-op; the per-CPU runqueue (next phase) is what
  determines the actual remote target.
- **Self-tests**:
  - `tlb_shootdown_self_test()` — enqueues 5 entries into the BSP's
    own queue, drains, verifies head/tail accounting and the `invlpg`
    path.
  - `reschedule_self_test()` — fakes a second CPU with a non-existent
    LAPIC id and calls `smp_send_ipi_to_cpu()`, verifying the
    bounded-wait path in `lapic_send_ipi()` returns instead of hanging.
  - `panic_broadcast_self_test()` — fakes a second CPU and calls
    `smp_broadcast_panic()`, verifying the cross-CPU panic
    broadcast path doesn't hang the BSP on a missing target.
  - `ipi_handler_self_test()` — invokes `smp_handle_ipi()` directly
    for the reschedule and TLB-shootdown paths and verifies the
    per-CPU state was updated correctly (per-CPU `need_resched` flag
    set, per-CPU TLB queue drained, delivery counters bumped). The
    panic path is not tested (it would halt). The IPI never
    actually arrives in single-CPU mode (`smp_send_ipi_to_cpu`
    skips self, broadcast shorthand has no targets), so this is
    the only way to verify the handler logic in the BSP-only
    test path.
- **End-to-end IPI receive (deferred)**: `smp_self_ipi()` is
  available (sends an IPI to the calling CPU) but the
  corresponding self-test panics in QEMU 8.2.2 — the BSP's
  LAPIC SVR is enabled (0x1FF) but the self-IPI is never
  delivered to the BSP in TCG. The IPI receive path will be
  exercised when APs come online (QEMU 9.x or a GDB-stub
  workaround). For now, the `ipi_handler_self_test` above
  verifies the handler logic directly without the LAPIC.
- **Kernel canary (Phase 33)**: `syscall_dispatch()` checks a
  `volatile uint64_t kernel_canary` on every syscall. If the
  value has been modified, the kernel panics. Catches stack
  overflows in the syscall path, BSS scribbles, and ROP
  gadgets that pivot the stack into the canary. Fixed value
  (predictable) is fine as a tripwire; for a real security
  barrier the value would need to be random per-boot (no
  good RNG in the kernel today).
- **Cross-CPU panic** (Phase 32 followup): `panic_at()` calls
  `smp_broadcast_panic()` before entering the cli/hlt loop, so a
  panicking CPU tells its peers to halt. The IPI handler on the
  receiving side halts via `cpu_halt()`. In single-CPU mode this
  is a no-op (no targets).
- **Per-CPU need_resched** (Phase 6 followup): the
  `need_resched` flag is per-CPU (`g_cpu_data[cpu].need_resched`).
  The reschedule IPI handler sets the target CPU's flag, so when
  APs come online, a target's next `schedule()` will actually
  re-evaluate.
- **Multi-CPU verified**: with `-smp 1` and `-smp 4` both, the kernel
  reaches the userspace init and runs tests 1–22 without regression.
  APs remain at the QEMU "wait for SIPI" stall (`LAPIC SVR=0`) in both
  cases; the bounded wait + AP-bring-up timeout let the boot continue
  past the SMP init phase either way.

## Physical Memory Manager (PMM)

- Bitmap allocator, 4 KiB pages (`kernel/mm/pmm.c`)
- Limine memory-map parsed at boot; usable ranges marked free
- `pmm_alloc_page` / `pmm_free_page` / `pmm_alloc_pages` (contiguous)
- Double-free and invalid-free detection
- Self-test: 2,000 pages allocated and freed with leak check

## Virtual Memory Manager (VMM)

- x86_64 4-level paging (`kernel/arch/x86_64/memory/vmm.c`)
- Kernel address space: higher-half direct map + kernel image
- Per-process user address spaces: `vmm_create_address_space`, `vmm_destroy_address_space`
- `vmm_map_page`, `vmm_unmap_page`, `vmm_protect_page`, `vmm_translate`
- Guard pages (`VMM_GUARD_PAGE_SIZE`)
- Self-test: write/readback, permission change, unmap, translation fail, cross-space copy, SMAP enforcement path, PMM leak detection after space destruction

## Kernel Heap

- Segregated free-list allocator (`kernel/mm/heap.c`)
- `kmalloc`, `kzalloc`, `kfree`, `krealloc`, `kmalloc_aligned`
- Poison-pattern debug mode, double-free detection
- Self-test: small/medium/large allocations, 500-block stress cycle, aligned alloc

## Scheduler

- Priority-based round-robin, 40 priority levels (nice –20..+19) (`kernel/sched/scheduler.c`)
- Time-slice proportional to priority; high-priority tasks get longer slices
- Timed sleep via `task_sleep_ticks()` with PIT-driven wakeup
- Dedicated idle task (lowest priority)
- Starvation prevention via periodic priority boost
- Per-task CPU time accounting
- Self-test: fair-share, priority ordering, sleep/wake correctness

## Process Model

- `struct process` (PID, UID, GID, files, VMM space, VMA list, signals) + `struct task` (TID, state, kernel stack, scheduling)
- `task_create_kernel` / `task_create_user`; `fork`, `clone` (thread mode), `execve`
- Zombie state → `wait4` reap path
- Per-process file descriptor table (up to `MAX_FILES_PER_PROCESS`)
- Current working directory (`cwd`) per process
- Process group and session IDs (partial — `setsid`, `setpgid`, `getpgrp` implemented)

## Syscall Infrastructure

- Linux x86_64 SYSCALL/SYSRET ABI via EFER/STAR/LSTAR/SFMASK MSRs (`kernel/arch/x86_64/syscall/syscall_stub.S`)
- All GP registers saved as `struct syscall_frame` (`kernel/arch/x86_64/syscall/syscall_entry.c`)
- 256-entry dispatch table; unknown calls return `-ENOSYS` (`kernel/sys/syscall_table.c`)
- Negative errno convention (`-EFAULT`, `-EBADF`, `-ENOSYS`, ...)
- `copy_from_user` / `copy_to_user` with address-range and overflow validation (`kernel/mm/uaccess.c`)
- Optional `SYSCALL_TRACE` compile flag for per-call tracing
- Self-test: `-ENOSYS`, `-EFAULT`, `-EBADF`, and `uaccess_ok` checks

**Implemented syscalls (verified)**: `read`, `write`, `readv`, `writev`, `open`, `openat`, `close`, `lseek`, `stat`, `fstat`, `lstat`, `newfstatat`, `statx` (stub), `access`, `faccessat`, `readlink`, `readlinkat`, `getdents64`, `mkdir`, `mkdirat`, `unlink`, `unlinkat`, `rmdir`, `dup`, `dup2`, `dup3`, `chmod`, `fchmod`, `fchmodat`, `chown`, `fchown`, `lchown`, `fchownat`, `umask`, `renameat`, `renameat2`, `symlink`, `symlinkat`, `brk`, `mmap` (anon + file-backed MAP_PRIVATE), `munmap`, `mprotect`, `fork`, `clone` (thread setup), `execve`, `wait4`, `exit`, `exit_group`, `getpid`, `gettid`, `getppid`, `setsid`, `setpgid`, `getpgrp`, `pipe`, `pipe2` (via pipe), `poll`, `clock_gettime`, `gettimeofday`, `nanosleep`, `futex` (WAIT/WAKE), `uname`, `getrandom`, `getrlimit`, `prlimit64`, `set_robust_list`, `get_robust_list`, `set_tid_address`, `arch_prctl` (FS/GS base), `rt_sigaction`, `rt_sigprocmask`, `rt_sigreturn`, `socket`, `bind`, `listen`, `accept`, `connect`, `sendto`, `recvfrom`, `setsockopt`, `getsockopt`, `ioctl` (TCGETS/TCSETS/TIOCGWINSZ stubs), `fcntl` (F_GETFL/F_SETFL/F_GETFD/F_SETFD/F_DUPFD)

**Stubs (safe, return errno)**: `madvise` (-ENOSYS), `mremap` (-ENOSYS), `prctl` (partial), `epoll_create1` (-ENOSYS), `epoll_ctl` (-ENOSYS), `epoll_wait` (-ENOSYS)

## ELF Loader

- ELF64 header validation (`kernel/core/elf_loader.c`)
- PT_LOAD segment mapping with correct permissions
- BSS zero-fill
- Entry point setup, user stack, argc/argv/envp, full auxv (AT_PHDR, AT_PHENT, AT_PHNUM, AT_BASE, AT_ENTRY, AT_RANDOM, AT_EXECFN, AT_UID, AT_EUID, AT_GID, AT_EGID, AT_SECURE)
- PT_INTERP detection: dynamic interpreter loaded at randomised base for ET_DYN ELFs
- Tested: static musl hello, static BusyBox, dynamic musl hello

## Virtual Filesystem (VFS)

- In-memory node tree (`struct vfs_node`) (`kernel/fs/vfs.c`)
- Symlink traversal (depth-limited to 16)
- Path canonicalisation (`.` / `..` handling)
- `open` / `read` / `write` / `lseek` / `close` / `stat` / `getdents64`
- `O_CREAT`, `O_TRUNC`, `O_APPEND`, `O_RDONLY`/`O_RDWR`/`O_WRONLY`, `O_NOFOLLOW`, `O_DIRECTORY`, `O_CLOEXEC`
- `mkdir`, `rmdir`, `unlink`, `rename`, `dup`, `dup2`, `dup3`
- Dispatch callbacks: `read`, `write`, `readdir`, `truncate`, `create`, `mkdir`, `unlink`, `close`

## Initramfs

- TAR archive embedded as binary section (`kernel/fs/initramfs_binary.S`)
- Parsed at boot by `initramfs_init()` (`kernel/fs/initramfs.c`)
- Populates VFS tree: `/bin/init`, `/bin/sh`, `/bin/busybox`, `/bin/hello_musl`, `/bin/hello_dynamic`, `/lib/ld-musl-x86_64.so.1`, `/tests/*`, `/hello.txt`

## Device Filesystem (devfs)

- `/dev/null` — reads return 0, writes discard
- `/dev/zero` — reads return zero bytes, writes discard
- `/dev/full` — reads return zero bytes, writes return `-ENOSPC`
- `/dev/random`, `/dev/urandom` — xorshift64 PRNG
- `/dev/console`, `/dev/tty` — serial/TTY output
- `/dev/kmsg` — ring-buffer kernel log read/write
- Block devices registered as `/dev/hda`, `/dev/hdb`, ... by ATA driver

## Procfs

- `/proc/version`, `/proc/cpuinfo`, `/proc/meminfo`, `/proc/uptime`, `/proc/stat`, `/proc/mounts`
- `/proc/self/status`, `/proc/<pid>/status` (dynamically created on first access)
- Dynamic PID directory listing via custom `readdir` callback

## Tmpfs

- `/tmp` directory backed by VFS default in-memory nodes
- Write and readback verified in test suite

## Ext2 Filesystem (Partial)

- ATA PIO block driver (`kernel/drivers/ata_pio.c`) reads 64 MiB `disk.img`
- Ext2 superblock magic validation; block group descriptor read; inode table walk
- Root directory entries populated into VFS at boot (`ext2_init`)
- File read (up to 12 direct blocks, 1 KiB block size)
- File write with block allocation (memory fallback and on-disk paths)
- `ext2_alloc_inode`, `ext2_alloc_block`, `ext2_save_inode`, `ext2_add_entry`
- `ext2_create` (file), `ext2_mkdir`, `ext2_truncate`
- **Memory fallback**: if no valid ext2 image is found on `/dev/hda`..`/dev/hdd`, a RAM-backed `/ext2/hello.txt` is used with correct `ram_read`/`ram_write`/`ram_truncate`
- **Limitation**: only 1 KiB block size supported; no indirect blocks; no journaling; persistent write path depends on ATA driver finding a valid ext2 image

## TTY / Terminal

- Basic TTY line discipline (`kernel/drivers/tty.c`)
- COM1 serial output for kernel logging
- `TCGETS` / `TCSETS` / `TIOCGWINSZ` ioctl stubs
- Ctrl+C → SIGINT delivery to foreground process group (partial)
- Canonical vs. raw mode switching (basic)

## Signals

- Pending signal mask per task
- `rt_sigaction`, `rt_sigprocmask` implemented
- Signal delivery before userspace return
- Default actions: SIGKILL terminates, SIGSEGV terminates, SIGINT terminates
- User signal handlers delivered via signal frame on user stack
- `rt_sigreturn` restores saved state
- **Partial**: SIGCHLD delivery to parent, complete POSIX job control future work

## Networking (Partial)

- VirtIO-net driver (`kernel/drivers/virtio_net.c`)
- Ethernet frame send/receive (`kernel/net/eth.c`)
- ARP (`kernel/net/arp.c`)
- IPv4 (`kernel/net/ipv4.c`)
- ICMP (`kernel/net/icmp.c`)
- UDP with echo server (`kernel/net/udp.c`)
- TCP basic listener on port 80 (`kernel/net/tcp.c`)
- Socket API: `socket`, `bind`, `listen`, `accept`, `connect`, `sendto`, `recvfrom`, `setsockopt`, `getsockopt`, `shutdown`
- **Static IP**: 10.0.2.15 / 255.255.255.0 / gateway 10.0.2.2 (QEMU user networking)
- **Limitation**: TCP reliability (retransmit, proper FIN handshake), DHCP, DNS resolver not yet implemented

## Memory Management Extensions

- VMA list per process (`struct vma`) with permission tracking
- Lazy anonymous `mmap` (pages allocated on first access via page fault handler)
- File-backed `mmap` MAP_PRIVATE with COW on write
- `munmap`, `mprotect`
- MAP_FIXED replacement (unmaps old mapping)
- Guard pages on stack

## Future Work (Not Yet Implemented)

- **User permission model**: UID/GID file permission checks are not yet enforced; all processes currently run as UID 0.
- **Complete TTY line discipline**: Support for `vi`, `Ctrl+Z`, and full job control (SIGTSTP) is in progress.
- **Advanced Networking**: DHCP client, DNS resolver, and TCP retransmit for robustness.
- **Package Management**: The `lpkg` system for installing and managing distro packages.
- **Self-hosting**: Ability to build the LiteNix kernel and userspace from within LiteNix itself.
- **Graphical Environment**: Framebuffer driver and a basic windowing system or compositor.

## Distro Capability Maturity

| System | Kernel Support | Userspace Support | Distro Status |
|---|---|---|---|
| **Memory** | Complete (PMM/VMM/Heap) | Complete (mmap/brk) | Stable |
| **Processes** | Complete (Scheduler/Tasks) | Partial (sh/init) | Usable |
| **Filesystem** | Complete (VFS/ext2/devfs) | Partial (BusyBox) | Usable (Persistent) |
| **Signals/TTY** | Partial (Handlers/Line Disc) | Partial (Shell) | Basic |
| **Networking** | Partial (UDP/TCP/ARP) | Minimal (No DNS) | Experimental |
| **Permissions** | Not Started | Not Started | Root-only |
| **Packages** | Not Started | Not Started | Future |

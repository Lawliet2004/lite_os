# LiteNix OS

LiteNix OS is a lightweight, Linux-like operating-system project for x86_64.
The first target is a small kernel that boots in QEMU through Limine and prints
diagnostics over both VGA text output and COM1 serial.

## Current Scope

LiteNix currently implements a functional x86_64 kernel foundation with partial
Linux-like userspace support. It is transitioning from a "teaching kernel" to a
"minimal CLI distro".

### Kernel Capabilities (Stable)
- **Boot**: Limine/BIOS, GDT, TSS, IDT, Exceptions, PIC, PIT.
- **Memory**: PMM (Bitmap), VMM (4-level paging), Segregated Free-list Heap.
- **Scheduler**: Priority-based round-robin with preemption and sleep/wake.
- **Syscalls**: Linux x86_64 `SYSCALL`/`SYSRET` ABI, 250+ entries, `uaccess` validation.
- **ELF Loader**: Static and dynamic ELF support (PT_INTERP), full auxiliary vectors.
- **VFS**: Core abstraction, path resolution, symlinks, `devfs`, `procfs`, `tmpfs`.

### Userspace Capabilities (Usable)
- **C Library**: Partial `libc-lite` and full `musl` (static/dynamic) support.
- **Init System**: `/bin/init` executes a full verification suite and starts a shell.
- **Shell**: BusyBox `sh` with core applets (`ls`, `cat`, `mkdir`, `rm`, `cp`, `echo`).
- **Filesystem**: Persistent EXT2 read/write on ATA disk (verified).
- **Networking**: ARP, IPv4, ICMP, UDP (Echo Server), TCP (Basic HTTP Server),
  `/sbin/dhcpcd` (DHCPv4 client), `/bin/hostname` backed by
  `/proc/sys/kernel/hostname`. `rcS.sh` prefers DHCP then falls back to a
  static `10.0.2.15` address. See [docs/network.md](docs/network.md).
- **Logging**: `/sbin/klogd` tails the kernel's 16 KiB `/dev/kmsg` ring
  buffer into `/var/log/kern.log` with timestamps; `/bin/logger`
  writes userspace messages to the kernel ring. `rcS.sh` starts
  klogd as a daemon and records a boot-completion marker. See
  [docs/logging.md](docs/logging.md).
- **Service supervision**: `/sbin/svc` is a C-based service
  controller (start/stop/restart/status/log/describe/tree/check/list/enable/disable)
  that parses `/etc/services.available/<name>.conf` files and
  honours `AFTER="..."` dependency ordering. `/sbin/supervisor`
  is a small respawn daemon that watches PID files and re-runs
  any `RESPAWN="yes"` service whose pid is dead. `rcS.sh` calls
  `svc start-enabled` then daemonises the supervisor. See
  [docs/supervisor.md](docs/supervisor.md).
- **User Management**: `/etc/passwd` + `/etc/shadow` with `$5$` SHA-256 hashed
  passwords, real `login`/`passwd`/`su`/`id`/`whoami`/`useradd`/`userdel`,
  kernel-enforced privilege drops via `setresuid`/`setresgid`, full VFS
  permission enforcement (mode-bit checks on open/unlink/mkdir/rename/chmod/
  chown), and S_ISUID-bit exec so `/bin/login` works for non-root users.
  See [docs/user-management.md](docs/user-management.md).

### Distro Capabilities (Experimental/Future)
- **TTY/Signals**: Basic line discipline and signal handlers work; full job control (SIGTSTP) is future work.
- **Permissions**: Full Unix mode-bit enforcement on open/unlink/mkdir/rename
  /chmod/chown, setuid bit honored in execve, `/etc/shadow` is mode 0600 and
  root-only-readable. Non-root users are properly sandboxed.
- **Packages**: `lpkg` package manager exists (custom `.lpkg` format,
  install/remove/verify/list); no remote signed repository yet.
- **Build System**: Currently requires a host cross-toolchain (Zig/Clang/Nasm).

It does not yet implement broad Linux application compatibility. Dynamic musl
hello and the current BusyBox shell smoke tests pass, but complete signals,
robust futex recovery, DHCP/DNS, and package management are future work.

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

This boots QEMU with a timeout and checks the serial log for VMM, heap,
scheduler initialization, networking listener, ext2, dynamic musl, BusyBox
shell, and persistent ext2 read markers. It also fails if the log contains an
init error, failed marker, CPU exception, or kernel panic.

Expected output:

```text
LiteNix kernel booted
Kernel status: OK
Boot verification passed
```

Phase 2/3 also prints:

```text
PMM: self-test passed (...)
VMM: self-test passed
VMM: address-space self-test passed
Heap: self-test passed
PIC: remapped
PIT: initialized at 100 Hz
Timer: ticks observed (...)
```

Current userspace verification output includes:

```text
Sched: initialized with 1 tasks (Phase 7)
Sched: timer preemption started
ext2: found /ext2/hello.txt
Hello from dynamic musl!
Test 23: PASSED
All VFS Verification Tests Passed!
Test 26: PASSED
All shell tests PASSED
Phase 9 & 10: init program exited with 0 OK
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

## Distro Experience

LiteNix now provides a "Mini Distro Experience v1" with an installer, package manager, and service management.

For details, see [docs/distro-experience.md](docs/distro-experience.md).

### Quick Commands:
- `litenix-install`: Install the OS to persistent disk.
- `lpkg`: Manage LiteNix packages.
- `service`: Manage system services.
- `login`, `passwd`, `su`, `useradd`, `userdel`: Multi-user management
  (default root password is `root`, see [docs/user-management.md](docs/user-management.md)).
- `hostname`, `ifconfig`, `dhcpcd`: Network configuration
  (DHCP preferred, falls back to static; see [docs/network.md](docs/network.md)).
- `klogd`, `logger`: Kernel ring buffer logging — see
  [docs/logging.md](docs/logging.md).

## Next Step

Keep the current verification matrix stable, then choose one small milestone:
strengthen persistent ext2 read/write reliability, improve signal/TTY behavior,
or add more bad-pointer syscall tests.

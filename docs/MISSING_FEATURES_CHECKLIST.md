# LiteNix Missing Features Checklist

**Quick reference for what's missing compared to production Linux distributions**

---

## ❌ CRITICAL BLOCKERS (Cannot run on real hardware)

### Hardware Support
- [ ] USB subsystem (UHCI/OHCI/EHCI/xHCI)
- [ ] USB HID (keyboard, mouse)
- [ ] USB storage
- [ ] USB network adapters
- [ ] Real Ethernet NICs (Intel e1000, Realtek r8169, Broadcom, etc.)
- [ ] SATA/AHCI storage controllers
- [ ] NVMe SSD support
- [ ] Graphics cards (Intel, AMD, NVIDIA drivers)
- [ ] Framebuffer/KMS for basic graphics
- [ ] PS/2 mouse
- [ ] Audio (ALSA drivers, Intel HDA, AC97, etc.)
- [ ] Wireless (WiFi drivers - ath9k, iwlwifi, rtw88, etc.)
- [ ] Bluetooth stack

### Core Kernel
- [ ] SMP (multi-core) support with per-CPU data structures
- [ ] APIC/x2APIC for multi-processor systems
- [ ] IOMMU support
- [ ] ACPI subsystem
- [ ] Power management (cpufreq, cpuidle, suspend/resume)
- [ ] High-resolution timers (hrtimer)
- [ ] Loadable kernel modules

---

## ⚠️ HIGH PRIORITY (Severely limits functionality)

### Networking
- [/] TCP reliability (FIN state transitions, TIME_WAIT, and segment retransmission timers with exponential backoff implemented; congestion control pending)
- [ ] IPv6 stack
- [ ] DHCP client
- [ ] Functional DNS resolver
- [ ] TLS/SSL (OpenSSL, mbedTLS, or similar)
- [ ] Netfilter/iptables (firewall)
- [ ] Advanced routing (multiple interfaces, policy routing)
- [ ] Network namespaces
- [ ] VLAN support
- [ ] Bridge/bonding
- [ ] VPN (OpenVPN, WireGuard)

### Filesystems
- [ ] ext4 with journaling and extents
- [ ] ext3 journaling
- [ ] ext2 indirect blocks (currently limited to 12KB files)
- [ ] XFS
- [ ] Btrfs with COW and snapshots
- [ ] F2FS for flash storage
- [ ] NTFS read/write
- [ ] FAT32/exFAT
- [ ] NFS client
- [ ] CIFS/SMB client
- [ ] FUSE (filesystem in userspace)
- [ ] SquashFS
- [ ] OverlayFS for containers

### Storage
- [ ] Page cache for file I/O
- [ ] Block I/O scheduler (CFQ, deadline, BFQ, etc.)
- [ ] Async I/O (io_uring, AIO)
- [ ] Direct I/O (O_DIRECT)
- [ ] LVM (Logical Volume Manager)
- [ ] Device mapper
- [ ] Software RAID (md)
- [ ] dm-crypt/LUKS encryption
- [ ] Swap space
- [ ] TRIM/discard for SSDs

### Memory Management
- [ ] Copy-on-write (COW) for fork()
- [ ] Transparent huge pages (THP)
- [ ] Memory compaction
- [ ] Out-of-memory (OOM) killer
- [ ] Kernel same-page merging (KSM)
- [ ] NUMA awareness
- [ ] Memory cgroups
- [ ] userfaultfd

### Process & Threading
- [ ] Full POSIX threads (pthread) support
- [ ] Namespaces (PID, mount, network, IPC, user, UTS, cgroup)
- [ ] Control groups (cgroups v1/v2)
- [ ] CPU affinity and pinning
- [ ] Process capabilities system
- [ ] Seccomp (syscall filtering)
- [ ] ptrace for debuggers (gdb, strace)
- [ ] Core dumps with full state

### Security
- [ ] User permission enforcement (currently all UID 0)
- [ ] SELinux or AppArmor (MAC)
- [ ] Kernel ASLR (KASLR)
- [ ] Userspace ASLR
- [ ] Stack canaries
- [ ] Control-flow integrity
- [ ] Audit subsystem
- [ ] /dev/random with proper entropy
- [ ] Kernel keyring

---

## 📝 MEDIUM PRIORITY (Quality of life)

### Shell & Utilities
- [/] Job control (Ctrl+Z key input / SIGTSTP signal routing implemented; shell fg/bg/jobs pending)
- [ ] Command history (up/down arrows)
- [ ] Tab completion
- [ ] Aliases and shell functions
- [ ] Advanced bash features
- [ ] grep with full regex
- [ ] sed stream editor
- [ ] awk text processing
- [ ] find utility
- [ ] Text editors (vim, nano, emacs)
- [ ] Man pages system
- [ ] GNU coreutils (full set of 100+ utilities)

### System Services
- [ ] systemd or equivalent init system
- [ ] udev for device management
- [ ] D-Bus message bus
- [ ] Network Manager
- [ ] systemd-journald structured logging
- [ ] systemd-resolved DNS
- [ ] systemd-timesyncd NTP
- [ ] Cron/timer daemon

### IPC Mechanisms
- [ ] Unix domain sockets (AF_UNIX)
- [ ] Named pipes (mkfifo)
- [ ] Message queues (POSIX and SysV)
- [ ] Semaphores (POSIX and SysV)
- [ ] Shared memory (shm_open, shmget)
- [ ] eventfd
- [ ] signalfd
- [ ] timerfd
- [ ] pidfd

### Development
- [ ] GCC/Clang native compilation in OS
- [ ] GDB debugger
- [ ] strace system call tracer
- [ ] valgrind memory debugger
- [ ] perf profiling tools
- [ ] Full glibc support (currently musl only)
- [ ] C++ standard library
- [ ] Python interpreter
- [ ] Build systems (CMake, autotools, Meson)

### Package Management
- [ ] Package repository (actual server with packages)
- [ ] Dependency resolution
- [ ] Package signing and verification
- [ ] Update system
- [ ] Thousands of packages (currently ~10)

---

## 🔧 LOW PRIORITY (Advanced features)

### Virtualization
- [ ] KVM hypervisor
- [ ] Xen support
- [ ] Hyper-V support
- [ ] Container runtime (Docker/Podman compatible)
- [ ] Nested virtualization

### Advanced Networking
- [ ] IPsec
- [ ] VXLAN/GENEVE
- [ ] eBPF/XDP packet processing
- [ ] TC (traffic control) QoS
- [ ] Multicast routing
- [ ] IPv6 routing

### Real-time
- [ ] SCHED_DEADLINE scheduler
- [ ] SCHED_FIFO/SCHED_RR
- [ ] Real-time kernel patches
- [ ] Preempt RT

### Graphics & Desktop
- [ ] DRM/KMS graphics subsystem
- [ ] X11 server
- [ ] Wayland compositor
- [ ] Desktop environment (GNOME, KDE, XFCE, etc.)
- [ ] Window manager
- [ ] GUI toolkit (GTK, Qt)
- [ ] Web browser
- [ ] Office suite

### Multimedia
- [ ] ALSA (Advanced Linux Sound Architecture)
- [ ] PulseAudio / PipeWire
- [ ] V4L2 (Video4Linux2) for cameras
- [ ] Media players (ffmpeg, VLC, mpv)
- [ ] Image editors (GIMP)
- [ ] Video editors

### Specialized
- [ ] Bluetooth networking
- [ ] Cellular modem support (4G/5G)
- [ ] NFC
- [ ] GPS
- [ ] Hardware sensors (lm-sensors)
- [ ] Thermal management
- [ ] Battery management
- [ ] Biometric devices

---

## ✅ WHAT YOU HAVE (For Reference)

### Kernel
- [x] Boot via Limine bootloader
- [x] GDT/IDT/TSS setup
- [x] PIC interrupt controller
- [x] PIT timer (100 Hz)
- [x] Exception handlers
- [x] PMM (physical memory manager)
- [x] VMM (virtual memory manager, 4-level paging)
- [x] Kernel heap allocator
- [x] Priority-based scheduler
- [x] Preemptive multitasking
- [x] Syscall interface (Linux x86_64 ABI)
- [x] User/kernel separation (ring 0/3)

### Processes
- [x] fork() / clone()
- [x] execve() with ELF loader (static + dynamic)
- [x] wait4()
- [x] exit() / exit_group()
- [x] Process groups (partial)
- [x] Sessions (setsid)
- [x] Basic signals (rt_sigaction, rt_sigprocmask, rt_sigreturn)

### Memory
- [x] brk() for heap
- [x] mmap() anonymous with lazy allocation
- [x] mmap() file-backed (MAP_PRIVATE)
- [x] munmap()
- [x] mprotect()
- [x] VMA tracking

### Filesystem
- [x] VFS layer
- [x] ext2 (partial: 1KB blocks, 12 direct blocks only)
- [x] tmpfs
- [x] initramfs
- [x] devfs (/dev/null, /dev/zero, /dev/full, /dev/random, /dev/urandom, /dev/console, /dev/tty, /dev/kmsg)
- [x] procfs (minimal: version, cpuinfo, meminfo, uptime, stat, mounts)
- [x] Symlinks
- [x] File operations (open, read, write, close, stat, lseek)
- [x] Directory operations (mkdir, rmdir, readdir)
- [x] rename, unlink

### Networking
- [x] VirtIO-net driver
- [x] Ethernet frames
- [x] ARP
- [x] IPv4 (no fragmentation)
- [x] ICMP (ping)
- [x] UDP
- [x] TCP (basic, no reliability)
- [x] Socket API (socket, bind, listen, accept, connect, sendto, recvfrom)
- [x] Static IP configuration

### Storage
- [x] ATA PIO driver
- [x] Block device abstraction

### Userspace
- [x] ELF loader (static and dynamic)
- [x] musl libc support
- [x] BusyBox shell (ls, cat, echo, mkdir, rm, cp, mv)
- [x] Custom utilities (lpkg, ifconfig, http_client, service)
- [x] Init system (basic)

### Synchronization
- [x] Spinlocks (via interrupt disable)
- [x] Wait queues
- [x] Futex (FUTEX_WAIT, FUTEX_WAKE, FUTEX_PRIVATE_FLAG)

---

## Completion Estimate

**Overall Progress:** ~5-8% of production Linux functionality

**By Category:**
- Kernel core: ~30% (basics work, missing SMP, modules, power mgmt)
- Hardware drivers: ~1% (only virtual devices, no real hardware)
- Networking: ~20% (protocols exist but unreliable, no IPv6/DHCP/DNS/TLS)
- Filesystems: ~15% (basic ext2, missing most other FSes and features)
- Security: ~5% (structures exist, not enforced)
- Userspace: ~2% (minimal shell, almost no applications)

---

## Realistic Development Timeline

**For a solo developer:**

- USB subsystem: **3-6 months**
- SMP support: **4-8 months**
- Real NIC drivers: **6-12 months** (per driver family)
- Graphics stack: **6-12 months** (basic framebuffer only)
- Desktop environment: **12-24 months**
- Full application ecosystem: **Not achievable solo**

**Estimated time to "usable on real hardware":** **3-5 years full-time**

**Estimated time to match Ubuntu feature-for-feature:** **10-20 years** (unrealistic for one person)

---

## Recommended Focus Areas

**If continuing development, prioritize:**

**Phase 1: Improve QEMU experience (3-6 months)**
1. TCP reliability (retransmission, proper state machine)
2. Functional DNS resolver
3. TLS/SSL support
4. Better shell (history, tab completion, job control)
5. Port standard utilities (grep, sed, awk, find)
6. Text editor (nano or micro)
7. Package repository with 50+ packages

**Phase 2: Stability and testing (3-6 months)**
8. Comprehensive test suite
9. Fix filesystem edge cases
10. Better error handling
11. Memory leak detection
12. Kernel debugging tools

**Phase 3: Real hardware (6-12+ months) ← Most projects stall here**
13. USB subsystem
14. Real Ethernet driver (e.g., Intel e1000)
15. SATA/AHCI storage
16. Basic framebuffer graphics
17. SMP support

---

## Key Takeaway

**LiteNix has accomplished the foundational 5-8% that demonstrates OS concepts beautifully. The remaining 92-95% is where decades of Linux development effort has been invested, primarily in:**

1. **Hardware support** (30-40% of gap)
2. **Advanced kernel features** (20-25% of gap)
3. **Complete networking** (15-20% of gap)
4. **Security infrastructure** (10-15% of gap)
5. **Application ecosystem** (5-10% of gap)

**The value of LiteNix is educational, not as a production OS replacement.**

---

**Last Updated:** June 13, 2026  
**Document Version:** 1.0

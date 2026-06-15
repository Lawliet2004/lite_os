# LiteNix OS - Complete AI Development Context

**Purpose:** This document provides comprehensive context for AI assistants to work effectively on the LiteNix OS project.

---

## Project Overview

**Name:** LiteNix OS  
**Type:** Custom x86_64 Operating System Kernel  
**Goal:** Educational/hobby OS with Linux syscall ABI compatibility  
**Current Status:** Phase 8 Complete (L4 Milestone - Usable CLI OS)  
**Lines of Code:** ~28,000 LOC (kernel + userspace)  
**Language:** C (kernel), Assembly (boot/low-level), Shell scripts (utilities)  
**Build System:** Make + Cross-compiler (Zig/Clang/NASM)  
**Target:** QEMU x86_64 (primary), VirtualBox (secondary)  

---

## Current Capabilities (What Works)

### Kernel Subsystems
- **Boot:** Limine bootloader (BIOS), higher-half kernel, serial + VGA text output
- **CPU:** GDT with ring 0/3 segments, IDT with 256 entries, TSS, legacy PIC (IRQ 32-47)
- **Interrupts:** Exception handlers (0-31), IRQ handlers, PIT timer at 100 Hz
- **Memory Management:**
  - PMM: Bitmap allocator for 4 KiB pages
  - VMM: x86_64 4-level paging, per-process address spaces
  - Heap: Segregated free-list allocator
  - VMA tracking (128 slots per process)
  - Lazy anonymous mmap, file-backed mmap (MAP_PRIVATE)
- **Scheduler:** Priority-based round-robin (40 levels), preemptive, sleep/wake queues
- **Processes:** fork, clone (threads), execve, wait4, exit, exit_group, process groups, sessions
- **Syscalls:** Linux x86_64 ABI via SYSCALL/SYSRET, 250+ entries, ~80 implemented
- **Signals:** rt_sigaction, rt_sigprocmask, rt_sigreturn, basic delivery, user handlers

### Filesystem
- **VFS:** In-memory node tree, symlinks (depth 16), path canonicalization
- **ext2:** Partial (1KB blocks, 12 direct blocks, no indirect, basic read/write)
- **tmpfs:** In-memory filesystem
- **initramfs:** TAR archive embedded in kernel
- **devfs:** /dev/null, /dev/zero, /dev/full, /dev/random, /dev/urandom, /dev/console, /dev/tty, /dev/kmsg
- **procfs:** /proc/version, /proc/cpuinfo, /proc/meminfo, /proc/uptime, /proc/stat, /proc/mounts, /proc/self/status

### Networking
- **Drivers:** VirtIO-net (basic)
- **Protocols:** Ethernet, ARP (cache), IPv4 (no fragmentation), ICMP (ping), UDP, TCP (basic, no retransmit)
- **Sockets:** AF_INET, SOCK_STREAM/SOCK_DGRAM, bind, listen, accept, connect, sendto, recvfrom
- **Config:** Static IP only (10.0.2.15/24, gateway 10.0.2.2)

### Storage
- **Drivers:** ATA PIO (legacy IDE), VirtIO-blk (stub)
- **Block devices:** /dev/hda, /dev/hdb, /dev/hdc, /dev/hdd

### Userspace
- **ELF Loader:** Static and dynamic ELF64, PT_INTERP, full auxv (AT_PHDR, AT_PHENT, AT_PHNUM, AT_BASE, AT_ENTRY, etc.)
- **C Library:** libc-lite (custom minimal), musl (static + dynamic)
- **Shell:** BusyBox sh with ls, cat, echo, mkdir, rm, cp, mv
- **Init:** Custom /bin/init with verification suite
- **Utilities:** lpkg (package manager), ifconfig, http_client, service, installer

### Verification Status
- 26 tests pass in init verification suite
- Boot verification: `make verify-boot` passes
- Serial log markers confirm all subsystems initialized
- Dynamic musl "Hello from dynamic musl!" works
- BusyBox shell "All shell tests PASSED" marker present
- Persistent ext2 read from /dev/hda verified

---

## Critical Limitations (What Doesn't Work)

### Kernel
- ❌ No multi-core (SMP) support - single CPU only
- ❌ No loadable kernel modules - everything compiled in
- ❌ No power management (ACPI, cpufreq, suspend/resume)
- ❌ No RCU, seqlocks, or advanced synchronization primitives
- ❌ No high-resolution timers (hrtimer) - only 100 Hz PIT
- ❌ No CPU frequency scaling or idle states

### Hardware
- ❌ No USB support (cannot use USB devices at all)
- ❌ No real network cards (only VirtIO, no Intel/Realtek/Broadcom drivers)
- ❌ No modern storage (SATA/AHCI/NVMe missing)
- ❌ No graphics drivers (VGA text mode only, no framebuffer/DRM)
- ❌ No input beyond partial PS/2 keyboard (no mouse, no touchpad)
- ❌ No audio (no sound card support)
- ❌ No wireless (no WiFi, no Bluetooth)

### Networking
- ❌ TCP is unreliable (no retransmission, no proper FIN handshake, no congestion control)
- ❌ No IPv6
- ❌ No DHCP client (static IP only)
- ❌ No functional DNS resolver
- ❌ No TLS/SSL (cannot access HTTPS)
- ❌ No firewall (no netfilter/iptables)
- ❌ No routing (single interface only)

### Filesystem
- ❌ ext2 limitations: 1KB blocks only, no indirect blocks, no journaling, no extents
- ❌ No ext3/ext4 (no journaling)
- ❌ No XFS, Btrfs, F2FS, NTFS, FAT32, etc.
- ❌ No page cache (synchronous I/O only)
- ❌ No I/O scheduler
- ❌ No async I/O (no io_uring, no AIO)

### Security
- ❌ User permissions not enforced (all processes run as UID 0)
- ❌ No SELinux or AppArmor
- ❌ No seccomp or capability system
- ❌ No ASLR, no PIE, no stack canaries in userspace
- ❌ No encryption (no TLS, no LUKS, weak PRNG)

### Userspace
- ❌ No desktop environment (no GUI at all)
- ❌ No package repository (lpkg exists but has ~10 packages max)
- ❌ No standard utilities (grep, sed, awk, find missing or incomplete)
- ❌ No text editors (no vim, nano, emacs)
- ❌ No development tools inside OS (GCC/Clang not ported)
- ❌ Shell lacks: history, tab completion, job control (Ctrl+Z)

---

## Architecture & Code Structure

### Directory Layout


```
LiteNix/
├── kernel/                    # Kernel source code
│   ├── arch/x86_64/          # x86_64 architecture-specific code
│   │   ├── boot/             # Boot assembly
│   │   ├── cpu/              # GDT, TSS
│   │   ├── interrupt/        # IDT, ISR, IRQ handlers, PIC
│   │   ├── memory/           # VMM, page table management
│   │   ├── sched/            # Context switching
│   │   └── syscall/          # Syscall entry/exit
│   ├── core/                 # Core kernel
│   │   ├── init.c            # Boot initialization
│   │   ├── kernel.c          # Main kernel entry point
│   │   ├── panic.c           # Panic handler
│   │   ├── printk.c          # Kernel logging
│   │   └── elf_loader.c      # ELF binary loader
│   ├── drivers/              # Device drivers
│   │   ├── ata_pio.c         # ATA PIO (IDE legacy)
│   │   ├── serial.c          # COM1 serial port
│   │   ├── vga_text.c        # VGA text mode
│   │   ├── pit.c             # Programmable Interval Timer
│   │   ├── tty.c             # TTY/terminal line discipline
│   │   ├── pci.c             # PCI bus enumeration
│   │   ├── virtio_net.c      # VirtIO network driver
│   │   └── virtio_blk.c      # VirtIO block driver (stub)
│   ├── fs/                   # Filesystem subsystem
│   │   ├── vfs.c             # Virtual filesystem layer
│   │   ├── ext2.c            # ext2 implementation
│   │   ├── initramfs.c       # Initramfs parser
│   │   └── block.c           # Block device abstraction
│   ├── lib/                  # Kernel library functions
│   │   ├── string.c          # memcpy, memset, strlen, etc.
│   │   └── setjmp.S          # setjmp/longjmp for panic recovery
│   ├── mm/                   # Memory management
│   │   ├── pmm.c             # Physical memory manager
│   │   ├── heap.c            # Kernel heap allocator
│   │   └── uaccess.c         # User pointer validation
│   ├── net/                  # Network stack
│   │   ├── net.c             # Network core
│   │   ├── eth.c             # Ethernet layer
│   │   ├── arp.c             # ARP protocol
│   │   ├── ipv4.c            # IPv4 protocol
│   │   ├── icmp.c            # ICMP protocol
│   │   ├── udp.c             # UDP protocol
│   │   ├── tcp.c             # TCP protocol (basic)
│   │   └── socket.c          # Socket API
│   ├── sched/                # Scheduler
│   │   ├── scheduler.c       # Round-robin scheduler
│   │   ├── task.c            # Process/thread management
│   │   └── wait_queue.c      # Sleep/wake primitives
│   ├── sys/                  # System calls
│   │   ├── syscall_table.c   # Syscall dispatch table
│   │   ├── sys_file.c        # File-related syscalls
│   │   ├── sys_process.c     # Process-related syscalls
│   │   ├── sys_mem.c         # Memory-related syscalls
│   │   ├── sys_exit.c        # exit/exit_group
│   │   └── sys_epoll.c       # epoll (partial)
│   └── include/              # Kernel headers
│       ├── arch/x86_64/      # x86_64-specific headers
│       ├── drivers/          # Driver headers
│       ├── fs/               # Filesystem headers
│       ├── kernel/           # Core kernel headers
│       ├── lib/              # Library headers
│       ├── mm/               # Memory management headers
│       ├── net/              # Network headers
│       ├── sched/            # Scheduler headers
│       └── sys/              # Syscall headers
├── user/                     # Userspace programs
│   ├── init/                 # Init process (PID 1)
│   │   └── init.c            # Verification suite + shell launcher
│   ├── libc-lite/            # Minimal C library
│   │   ├── libc_lite.c       # Implementation
│   │   └── libc_lite.h       # Header
│   ├── shell/                # Simple shell
│   │   └── sh.c              # Shell implementation
│   ├── services/             # System services
│   │   ├── http_server.c     # Basic HTTP server
│   │   └── udp_echo.c        # UDP echo server
│   ├── tests/                # Test programs
│   │   ├── compat_abi.c      # ABI compatibility tests
│   │   └── hello_pkg.c       # Simple test program
│   ├── lpkg.c                # Package manager
│   ├── http_client.c         # HTTP downloader
│   ├── dns_resolve.c         # DNS resolver (partial)
│   ├── ifconfig.c            # Network configuration tool
│   ├── login.c               # Login prompt (stub)
│   ├── passwd.c              # Password utility (stub)
│   ├── installer.sh          # System installer
│   ├── service.sh            # Service manager
│   └── rcS.sh                # Init script
├── tests/                    # External test suites
│   ├── syscall/              # Syscall tests
│   ├── userspace/            # Userspace integration tests
│   │   ├── busybox/          # BusyBox test binaries
│   │   ├── dynamic-musl/     # Dynamic linking tests
│   │   └── raw-syscall/      # Direct syscall tests
│   └── kernel/               # Kernel unit tests
├── scripts/                  # Build and test scripts
│   ├── make_ext2.py          # ext2 image generator
│   ├── make_initramfs.py     # Initramfs TAR creator
│   ├── make_lpkg.py          # Package creator
│   ├── check_toolchain.py    # Toolchain verification
│   ├── test_boot.sh          # Boot verification
│   └── ci-boot.ps1           # CI script for Windows
├── boot/                     # Boot configuration
│   └── limine.conf           # Limine bootloader config
├── toolchain/                # External toolchain
│   └── limine/               # Limine bootloader files
├── docs/                     # Documentation
│   ├── architecture.md       # Architecture overview
│   ├── roadmap.md            # Development roadmap
│   ├── syscall-roadmap.md    # Syscall implementation status
│   ├── compatibility.md      # Linux compatibility matrix
│   ├── linux-os-plan.md      # Linux-like OS plan
│   ├── memory.md             # Memory management docs
│   ├── scheduler.md          # Scheduler documentation
│   ├── build.md              # Build instructions
│   └── virtualbox.md         # VirtualBox setup
├── build/                    # Build output (generated)
│   ├── litenix.elf           # Kernel binary
│   ├── litenix.iso           # Bootable ISO
│   ├── disk.img              # Persistent disk image (ext2)
│   ├── rootfs.img            # Root filesystem image
│   ├── initramfs.tar         # Initramfs archive
│   └── serial.log            # Serial output log
├── Makefile                  # Main build file
└── README.md                 # Project overview
```

---

## Key Data Structures

### Process & Task Management
```c
struct process {
    uint64_t pid;                  // Process ID
    uint64_t ppid;                 // Parent PID
    uint64_t pgid;                 // Process group ID
    uint64_t sid;                  // Session ID
    uint64_t ruid, euid, suid;     // User IDs
    uint64_t rgid, egid, sgid;     // Group IDs
    struct address_space *vm;      // Virtual memory space
    struct file *files[MAX_FILES]; // File descriptor table
    struct vfs_node *cwd;          // Current working directory
    struct vma *vma_list;          // VMA linked list
    sigaction_linux sigactions[64];// Signal handlers
    uint64_t pending_signals;      // Pending signal mask
    uint64_t blocked_signals;      // Blocked signal mask
    struct task *tasks;            // Threads in this process
    struct process *children;      // Child processes
    // ... more fields
};

struct task {
    uint64_t tid;                  // Thread ID
    enum task_state state;         // RUNNING, SLEEPING, ZOMBIE
    uint64_t priority;             // Scheduler priority
    uint64_t time_slice;           // Remaining time slice
    uint64_t total_ticks;          // CPU time used
    struct process *process;       // Parent process
    void *kernel_stack;            // Kernel stack pointer
    struct syscall_frame *user_frame; // Saved user registers
    struct task *next;             // Scheduler queue link
    // ... more fields
};
```

### Memory Management
```c
struct vma {                       // Virtual Memory Area
    uint64_t start;                // Start address
    uint64_t end;                  // End address (exclusive)
    uint32_t prot;                 // Protection flags
    uint32_t flags;                // MAP_PRIVATE, MAP_SHARED, etc.
    struct file *file;             // Backing file (if file-backed)
    uint64_t offset;               // File offset
    struct vma *next;              // Linked list
};

struct address_space {
    uint64_t pml4_phys;            // Top-level page table physical address
    struct vma *vma_list;          // VMA list for this address space
    uint64_t heap_start;           // Heap start
    uint64_t heap_end;             // Current heap end (brk)
};
```

### Filesystem
```c
struct vfs_node {
    char name[256];                // Node name
    enum vfs_node_type type;       // FILE, DIR, SYMLINK, CHARDEV, BLOCKDEV
    uint64_t size;                 // File size
    uint32_t mode;                 // Permissions (rwxrwxrwx)
    uint32_t uid, gid;             // Owner
    struct vfs_node *children;     // Directory children
    struct vfs_node *next;         // Sibling link
    void *fs_specific;             // Filesystem-specific data
    // Function pointers for operations:
    int64_t (*read)(struct vfs_node *, void *, uint64_t, uint64_t);
    int64_t (*write)(struct vfs_node *, const void *, uint64_t, uint64_t);
    int64_t (*readdir)(struct vfs_node *, struct vfs_dirent *, uint64_t);
    // ... more ops
};

struct file {
    struct vfs_node *node;         // VFS node
    uint64_t offset;               // Current position
    uint32_t flags;                // O_RDONLY, O_WRONLY, O_RDWR, etc.
    uint32_t refcount;             // Reference count
};
```

### Networking
```c
struct socket {
    uint32_t domain;               // AF_INET
    uint32_t type;                 // SOCK_STREAM, SOCK_DGRAM
    uint32_t protocol;             // IPPROTO_TCP, IPPROTO_UDP
    uint16_t local_port;           // Bound local port
    uint16_t remote_port;          // Connected remote port
    uint32_t local_ip;             // Bound local IP
    uint32_t remote_ip;            // Connected remote IP
    enum tcp_state tcp_state;      // TCP state machine
    struct wait_queue rx_wq;       // Receive wait queue
    struct wait_queue tx_wq;       // Transmit wait queue
    // Buffers:
    uint8_t rx_buffer[SOCKET_RX_BUFFER_SIZE];
    uint64_t rx_head, rx_tail;
    // ... more fields
};
```

---

## Syscall Implementation Pattern

When implementing a syscall, follow this pattern:

```c
int64_t sys_example(struct syscall_frame *frame) {
    // Extract arguments from frame
    uint64_t arg1 = frame->rdi;
    void __user *uptr = (void __user *)frame->rsi;
    
    // Validate arguments
    if (!vmm_validate_user_ptr(current_task->process->vm, uptr, size)) {
        return -EFAULT;
    }
    
    // Perform operation with error handling
    int result = do_something();
    if (result < 0) {
        return result;  // Return negative errno
    }
    
    // Copy result to userspace if needed
    if (copy_to_user(uptr, &data, sizeof(data)) != 0) {
        return -EFAULT;
    }
    
    return result;  // Return positive value or 0 on success
}
```

**Critical Rules:**
1. Always validate user pointers with `vmm_validate_user_ptr()`
2. Use `copy_from_user()` / `copy_to_user()` for user memory access
3. Return negative errno values on error (-EFAULT, -EINVAL, -ENOSYS, etc.)
4. Never directly dereference user pointers in kernel space
5. Check for NULL pointers before validation
6. Handle arithmetic overflow in size calculations

---

## Build System

### Prerequisites
- **Compiler:** Clang/Zig with x86_64 freestanding target, or x86_64-elf-gcc
- **Assembler:** NASM
- **Linker:** ld.lld or x86_64-elf-ld
- **ISO creation:** xorriso
- **Emulator:** QEMU (qemu-system-x86_64)
- **Python:** For build scripts

### Build Commands
```bash
# Build kernel
make

# Build ISO image
make iso

# Run in QEMU
make run

# Verify boot (all tests must pass)
make verify-boot

# Clean build artifacts
make clean
```

### Build Flags
```c
// Kernel CFLAGS
-std=c11 -ffreestanding -fno-builtin -fno-stack-protector
-fno-pic -fno-pie -m64 -mcmodel=kernel -mno-red-zone
-fno-omit-frame-pointer -mno-mmx -mno-sse -mno-sse2
-msoft-float -Wall -Wextra -Werror -O2 -g

// Userspace CFLAGS
-std=c11 -ffreestanding -fno-builtin -fno-stack-protector
-fno-pic -fno-pie -m64 -fno-omit-frame-pointer
-mno-mmx -mno-sse -mno-sse2 -Wall -Wextra -Werror -O2 -g
```

---

## Debugging

### QEMU Serial Output
All kernel logging goes to serial port (COM1), captured in `build/serial.log`:
```bash
# View serial log
cat build/serial.log

# Watch serial log in real-time
tail -f build/serial.log
```

### Verification Markers
Boot verification checks for these markers in serial.log:
- `VMM: address-space self-test passed`
- `Heap: self-test passed`
- `Sched: initialized`
- `Hello from dynamic musl!`
- `All shell tests PASSED`
- `Phase 9 & 10: init program exited with 0 OK`

**Failure markers (must NOT appear):**
- `Init ERROR`
- `KERNEL PANIC`
- `CPU exception` (outside controlled tests)

### GDB Debugging
```bash
# In terminal 1: Start QEMU with GDB stub
make debug-gdb

# In terminal 2: Connect GDB
gdb build/litenix.elf
(gdb) target remote :1234
(gdb) break kernel_main
(gdb) continue
```

---

## Common Development Patterns

### Adding a New Syscall

1. **Add to syscall table** (`kernel/sys/syscall_table.c`):
```c
[SYS_newsyscall] = sys_newsyscall,
```

2. **Declare in header** (`kernel/include/sys/syscall.h`):
```c
int64_t sys_newsyscall(struct syscall_frame *frame);
```

3. **Implement** (e.g., `kernel/sys/sys_process.c`):
```c
int64_t sys_newsyscall(struct syscall_frame *frame) {
    // Implementation
}
```

4. **Update documentation** (`docs/syscall-roadmap.md`)

5. **Add test** (`tests/syscall/test_newsyscall.c` or in `user/init/init.c`)

### Adding a VFS Operation

1. **Add operation to vfs_node**:
```c
struct vfs_node {
    // ...
    int64_t (*new_op)(struct vfs_node *, /* args */);
};
```

2. **Implement for each filesystem** (ext2, devfs, procfs, etc.)

3. **Call from VFS layer** (`kernel/fs/vfs.c`)

### Adding a Device Driver

1. **Create driver file** (`kernel/drivers/new_device.c`)

2. **Implement initialization**:
```c
void new_device_init(void) {
    // Detect and initialize hardware
    // Register with appropriate subsystem (block, net, char)
}
```

3. **Call from kernel_main** (`kernel/core/kernel.c`):
```c
new_device_init();
```

4. **Add to Makefile** (`Makefile`):
```c
C_SOURCES += kernel/drivers/new_device.c
```

---

## Testing Guidelines

### Test Levels

1. **Unit Tests:** Test individual functions (minimal in LiteNix)
2. **Integration Tests:** Test subsystem interactions (in `user/init/init.c`)
3. **Boot Verification:** Full system test via `make verify-boot`

### Writing a Test

Add to `user/init/init.c`:
```c
// Test N: Description
{
    int result = test_something();
    if (result != 0) {
        printf("Init ERROR: Test N failed\\n");
        exit(1);
    }
    printf("Test N: PASSED\\n");
}
```

### Verification Requirements

For a feature to be "verified":
1. Code must compile without warnings
2. Boot verification must pass (no KERNEL PANIC)
3. Serial log must contain success marker for the feature
4. No regression in existing tests

---

## Code Style & Conventions

### Naming
- **Functions:** `snake_case` (e.g., `vmm_map_page`, `sys_read`)
- **Types:** `snake_case` (e.g., `struct task`, `struct vfs_node`)
- **Constants:** `UPPER_SNAKE_CASE` (e.g., `PAGE_SIZE`, `MAX_FILES`)
- **Macros:** `UPPER_SNAKE_CASE`

### File Organization
- **One subsystem per directory**
- **Implementation in `.c` files**
- **Public API in `kernel/include/*/` headers**
- **Private helpers can be static**

### Error Handling
- Kernel functions return negative errno values or 0 on success
- Use `printk` for kernel diagnostics
- Use `panic()` for unrecoverable errors
- Validate all inputs, especially user pointers

### Memory Management
- Always free allocated memory
- Use `kmalloc` / `kfree` in kernel
- Use `pmm_alloc_page` / `pmm_free_page` for physical pages
- Track allocations to avoid leaks

---

## Known Issues & Limitations

### Critical Bugs (None currently known—boot verification passes)

### Known Limitations
1. **ext2:** 1KB blocks only, no indirect blocks, max file size ~12KB
2. **TCP:** No retransmission, no proper FIN handshake, no flow control
3. **Signals:** Partial implementation, no SIGCHLD to parent, no job control
4. **TTY:** Basic line discipline, no full POSIX termios
5. **select/epoll:** Returns immediately, doesn't truly block
6. **User permissions:** Structure exists but not enforced
7. **Single-core:** No SMP support

### Future Work Priority
1. TCP reliability (retransmission, proper state machine)
2. DNS resolver integration
3. TLS/SSL support (mbedTLS port)
4. Shell improvements (history, tab completion, Ctrl+Z)
5. More standard utilities (grep, sed, awk, find)
6. USB subsystem (huge undertaking)
7. Real NIC drivers (e.g., Intel e1000)
8. Multi-core SMP support

---

## AI Assistant Guidelines

When working on LiteNix:

### DO:
✅ Read existing code to understand patterns before adding new code
✅ Follow the existing code style and naming conventions
✅ Add comprehensive error handling (check pointers, validate sizes, return errno)
✅ Update documentation when adding features
✅ Add tests for new functionality
✅ Explain trade-offs and design decisions
✅ Check `build/serial.log` after changes to verify boot
✅ Maintain compatibility with existing verified tests

### DON'T:
❌ Break existing verified functionality
❌ Skip error handling or input validation
❌ Dereference user pointers directly in kernel
❌ Assume features exist (USB, graphics, etc.) unless verified
❌ Add features without tests
❌ Ignore compiler warnings
❌ Claim something works without serial log evidence
❌ Copy code from other projects without understanding licensing

### When Implementing Features:
1. **Understand the requirement:** What specific functionality is needed?
2. **Check existing code:** How do similar features work?
3. **Design the interface:** What syscalls/APIs are needed?
4. **Implement incrementally:** Add one piece at a time
5. **Test at each step:** Verify boot doesn't break
6. **Document:** Update roadmap, compatibility matrix, architecture docs
7. **Verify:** Get serial log evidence of functionality

### When Debugging:
1. **Check serial.log:** Look for error messages, panics, exceptions
2. **Add printk:** Strategic logging to trace execution
3. **Use GDB:** For complex issues requiring step-through
4. **Simplify:** Create minimal test case reproducing the issue
5. **Read the spec:** Consult x86_64 manuals, Linux man pages, POSIX specs

---

## Reference Resources

### Specifications & Manuals
- **Intel SDM:** Intel 64 and IA-32 Architectures Software Developer's Manual (volumes 1-4)
- **AMD APM:** AMD64 Architecture Programmer's Manual
- **x86_64 ABI:** System V Application Binary Interface AMD64 Architecture Processor Supplement
- **Linux man pages:** `man 2 syscall_name` for syscall specifications
- **POSIX:** IEEE Std 1003.1 (POSIX.1-2017)

### External Code & Tools
- **Limine:** Modern bootloader (https://github.com/limine-bootloader/limine)
- **BusyBox:** Embedded Linux utilities (https://busybox.net/)
- **musl libc:** Lightweight C library (https://musl.libc.org/)

### Learning Resources
- **OSDev Wiki:** https://wiki.osdev.org/
- **Linux source:** https://elixir.bootlin.com/linux/latest/source (for reference, not copying)

---

## Example AI Prompts for Common Tasks

### Adding a Syscall
```
I need to implement the <syscall_name> syscall for LiteNix. 

Requirements:
- Linux syscall number: <number>
- Arguments: <arg1>, <arg2>, ...
- Expected behavior: <description>

Please:
1. Show the syscall table entry
2. Implement the syscall handler with proper error handling
3. Add appropriate tests
4. Update syscall-roadmap.md
```

### Debugging an Issue
```
LiteNix is experiencing <issue description>. The serial log shows:
<paste relevant serial.log excerpt>

The issue occurs when <conditions>.

Please help diagnose and fix this issue, considering:
1. What subsystems are involved?
2. What could cause this symptom?
3. How to add debugging output?
4. What's the likely fix?
```

### Adding a Driver
```
I want to add a driver for <hardware device> to LiteNix.

Device specifications:
- Type: <block/network/character>
- Interface: <PCI/ISA/etc>
- Registers: <description>

Please:
1. Outline the driver architecture
2. Show initialization code
3. Implement basic operations (read/write/etc)
4. Integrate with existing subsystems (VFS/network/block)
```

---

## Project Philosophy

**LiteNix is:**
- An educational project demonstrating OS concepts
- A clean, understandable codebase for learning
- A platform for exploring OS design without legacy constraints
- A rewarding hobby and technical challenge

**LiteNix is NOT:**
- A production-ready operating system
- A Linux replacement for daily use
- Aiming for feature parity with Ubuntu/Arch/etc
- Designed to run on arbitrary real hardware

**Development Priorities:**
1. **Correctness:** Code should be correct and well-tested
2. **Clarity:** Code should be readable and well-documented
3. **Completeness:** Features should be implemented fully, not half-done
4. **Compatibility:** Should follow Linux ABI and POSIX where practical

**When in doubt:**
- Prefer simplicity over clever optimization
- Document trade-offs and design decisions
- Test thoroughly before claiming functionality
- Be honest about limitations

---

## Quick Reference Commands

```bash
# Build and test
make                     # Build kernel
make iso                 # Build bootable ISO
make run                 # Run in QEMU (interactive)
make verify-boot         # Automated boot test (must pass)
make clean               # Clean build artifacts

# Debugging
tail -f build/serial.log # Watch serial output
make debug-gdb           # Start QEMU with GDB stub

# Checking status
cat build/serial.log | grep "PASSED"    # See passing tests
cat build/serial.log | grep "ERROR"     # Check for errors
cat build/serial.log | grep "PANIC"     # Check for panics
```

---

## Contact & Contribution

- **Documentation:** See `docs/` directory for detailed information
- **Issues:** Document bugs in serial.log with reproduction steps
- **Features:** Check roadmap.md for planned work
- **Questions:** Refer to architecture.md and this document first

---

**Last Updated:** June 13, 2026  
**Document Version:** 1.0  
**LiteNix Version:** Phase 8 Complete (L4 Milestone)

---

## AI Assistant Summary

You now have complete context on LiteNix OS:
- ✅ Current capabilities and verified features
- ✅ Known limitations and missing functionality
- ✅ Code structure and architecture
- ✅ Development patterns and conventions
- ✅ Build system and toolchain
- ✅ Testing and verification requirements
- ✅ Project philosophy and goals

**You can now effectively:**
- Implement new features following existing patterns
- Debug issues using serial logs and code analysis
- Add syscalls with proper error handling
- Extend subsystems (VFS, network, drivers)
- Write tests and verify functionality
- Update documentation accurately
- Provide realistic assessments of feasibility

**Remember:**
- Always maintain boot verification passing
- Follow error handling patterns strictly
- Test incrementally
- Document changes
- Be honest about limitations

Ready to assist with LiteNix development! 🚀

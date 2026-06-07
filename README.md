# LiteNix OS

LiteNix OS is a lightweight, Linux-like operating-system project for x86_64.
The first target is a small kernel that boots in QEMU through Limine and prints
diagnostics over both VGA text output and COM1 serial.

## Current Scope

This repository currently implements Phase 0, Phase 1, and the first Phase 2
CPU-exception path:

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
- VMM self-test using a PMM-backed virtual mapping
- QEMU and ISO helper scripts

It does not implement a scheduler, user mode, ELF loading, syscalls, VFS,
keyboard input processing, or Linux application compatibility yet.

## Linux Compatibility Policy

LiteNix must not claim to run all Linux applications. Compatibility is measured
level by level, with each level backed by explicit test programs and serial-log
evidence. See [docs/linux-compat.md](docs/linux-compat.md) and
[docs/syscall-roadmap.md](docs/syscall-roadmap.md).

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

Continue Phase 4 by adding standalone address-space creation/destruction and
guarded user pointer validation helpers. Phase 5 is the kernel heap.

# LiteNix Build Documentation

LiteNix provides a CI-friendly build system using `make` and `python`.

## Prerequisites

- **Make**: Standard GNU `make`.
- **Compiler**: `zig cc` (vendored or system) or `clang` for x86_64 freestanding targets.
- **Assembler**: `nasm`.
- **Python**: Python 3 for utility scripts.
- **ISO Creation**: `xorriso`.
- **Emulator**: QEMU (`qemu-system-x86_64`) for running and testing.

## Verifying Toolchain

To ensure your environment has all the required tools to build and test LiteNix:

```sh
make check-toolchain
```

This will run `scripts/check_toolchain.py` and verify Zig, NASM, Xorriso, QEMU, and the Limine bootloader files. It handles MSYS2 environments on Windows automatically.

## Standard Build

To build the kernel and all intermediate userspace binaries:

```sh
make all
```

To build the bootable ISO image:

```sh
make iso
```

To build the EXT2 root filesystem image containing the userspace utilities and testing binaries:

```sh
make rootfs
```

## Running Tests

LiteNix has built-in verification targets that are ideal for Continuous Integration (CI).

### Normal Verification
Boot the OS and run standard tests. QEMU will run headlessly, capture serial output, and `make` will inspect the log for successful initialization and test completion markers.

```sh
make verify-boot
```

### Persistent Reboot Verification
This checks that files survive across a reboot on the EXT2 filesystem.

```sh
make verify-persistent
```

### Negative Testing (Fault Handling)
Verify that the kernel correctly catches and panics on illegal operations.

```sh
make verify-boot-vmm
make verify-boot-heap
```

### Full Verification Suite
To run all boot tests:

```sh
make verify-boot-all
```

## Continuous Integration (CI) Usage

In a CI environment, you should run:

```sh
make check-toolchain
make verify-boot-all
make verify-persistent
```

These commands will return non-zero exit codes if any test fails, making them perfect for automated pipelines.

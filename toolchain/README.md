# Toolchain Setup

LiteNix expects a freestanding x86_64 compiler, NASM, QEMU, `xorriso`, and
Limine.

## Compiler

Use either:

- `clang` configured for a freestanding x86_64 target
- `x86_64-elf-gcc`

On Unix-like hosts, the default `Makefile` uses `clang --target=x86_64-elf`
and LLD. You can override `CC`, `LD`, and `OBJCOPY` when using a dedicated
cross GCC toolchain.

On Windows/MSYS2, the default `Makefile` uses Zig's C compiler driver:

```sh
zig cc -target x86_64-freestanding-none
```

This avoids MinGW LLD driver-mode issues when linking a freestanding ELF
kernel.

Install the working Windows package set from an MSYS2 shell:

```sh
pacman -S --needed make nasm xorriso mingw-w64-ucrt-x86_64-qemu mingw-w64-clang-x86_64-zig
```

## Limine

Install Limine locally under:

```text
toolchain/limine/
```

Expected files:

```text
toolchain/limine/limine
toolchain/limine/limine.exe
toolchain/limine/limine-bios.sys
toolchain/limine/limine-bios-cd.bin
```

`limine.exe` is used on Windows. The Unix `limine` host tool is used on other
hosts.

The Phase 0-2 ISO path is BIOS-only. UEFI support will be added later with the
current Limine `BOOTX64.EFI` flow.

# Toolchain Setup

LiteNix Phase 0/1 expects a freestanding x86_64 compiler, NASM, QEMU,
`xorriso`, and Limine.

## Compiler

Use either:

- `clang` configured for a freestanding x86_64 target
- `x86_64-elf-gcc`

The default `Makefile` uses `clang --target=x86_64-elf` and LLD. You can
override `CC`, `LD`, and `OBJCOPY` when using a dedicated cross GCC toolchain.

## Limine

Install Limine locally under:

```text
toolchain/limine/
```

Expected files:

```text
toolchain/limine/limine
toolchain/limine/limine-bios.sys
toolchain/limine/limine-bios-cd.bin
```

The Phase 0-2 ISO path is BIOS-only. UEFI support will be added later with the
current Limine `BOOTX64.EFI` flow.

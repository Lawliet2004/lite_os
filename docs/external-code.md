# External Code And Artifacts

LiteNix should keep external code and binaries explicit.

## Limine

- Source: https://github.com/limine-bootloader/limine
- Version used for local validation: current shallow clone at build time
- Reason: BIOS bootloader and ISO boot support
- Files copied into `toolchain/limine/`:
  - `limine`
  - `limine-bios.sys`
  - `limine-bios-cd.bin`

These are bootloader artifacts, not kernel code. LiteNix kernel source remains
an original implementation.

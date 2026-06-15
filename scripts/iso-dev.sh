#!/usr/bin/env bash
# scripts/iso-dev.sh
# Build a bootable ISO from build/litenix.elf using the existing limine tools.

set -euo pipefail

cd "$(dirname "$0")/.."
ROOT="$(pwd)"

ISO_ROOT="$ROOT/build/iso-root"
rm -rf "$ISO_ROOT"
mkdir -p "$ISO_ROOT/boot"

cp "$ROOT/build/litenix.elf" "$ISO_ROOT/boot/litenix.elf"
cp "$ROOT/boot/limine.conf" "$ISO_ROOT/boot/limine.conf"
cp "$ROOT/toolchain/limine/limine-bios.sys" "$ISO_ROOT/boot/"
cp "$ROOT/toolchain/limine/limine-bios-cd.bin" "$ISO_ROOT/boot/"

xorriso -as mkisofs -b boot/limine-bios-cd.bin \
  -no-emul-boot -boot-load-size 4 -boot-info-table \
  "$ISO_ROOT" -o "$ROOT/build/litenix.iso"

# Run the Limine BIOS installer to write the MBR/boot record
"$ROOT/toolchain/limine/limine.exe" bios-install "$ROOT/build/litenix.iso" || {
  echo "limine bios-install failed (this is OK for QEMU cdrom boot)"
}

echo "Built $ROOT/build/litenix.iso"
ls -la "$ROOT/build/litenix.iso"

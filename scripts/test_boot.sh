#!/bin/bash
export PATH="/usr/bin:$PATH"
rm -f build/serial.log
timeout -k 5s 15s /c/msys64/ucrt64/bin/qemu-system-x86_64 -M pc -m 64M -cdrom build/litenix.iso -serial file:build/serial.log -debugcon stdio -no-reboot -no-shutdown -display none -netdev user,id=n0 -device virtio-net-pci,netdev=n0 -drive file=build/disk.img,if=ide,format=raw
echo "QEMU_EXIT_CODE=$?"

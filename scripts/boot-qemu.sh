#!/usr/bin/env bash
# Boot LiteNix ISO in QEMU with a wall-clock timeout.
# Usage: bash scripts/boot-qemu.sh <logname> [smp_count]
set -e
LOG="$1"
SMP="${2:-1}"
ISO="/mnt/c/Users/Papan Ghosh/Desktop/Projects/Operating System/build/litenix.iso"
OUT="/mnt/c/Users/Papan Ghosh/Desktop/Projects/Operating System/build/${LOG}.log"
echo "[boot-qemu] iso=$ISO out=$OUT smp=$SMP"
qemu-system-x86_64 \
  -M pc -m 128M \
  -cdrom "$ISO" \
  -serial "file:$OUT" \
  -debugcon stdio \
  -no-reboot -no-shutdown \
  -display none \
  -netdev user,id=n0 -device virtio-net-pci,netdev=n0 \
  -smp "$SMP" &
QPID=$!
echo "[boot-qemu] pid=$QPID"
# Wait up to 300s for QEMU to exit on its own, then kill.
for i in $(seq 1 300); do
    if ! kill -0 $QPID 2>/dev/null; then
        echo "[boot-qemu] qemu exited after ${i}s"
        break
    fi
    sleep 1
done
if kill -0 $QPID 2>/dev/null; then
    echo "[boot-qemu] killing qemu after 300s"
    kill $QPID 2>/dev/null || true
    sleep 2
    kill -9 $QPID 2>/dev/null || true
fi
wait $QPID 2>/dev/null || true
echo "[boot-qemu] done"

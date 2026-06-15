#!/usr/bin/env bash
# Phase 8: QEMU monitor debug. We start QEMU with a Unix-socket
# monitor, then send a series of HMP commands to inspect CPU
# state, LAPIC state, and the trampoline's diag region. The
# kernel boots in ~1-2 s; the APs either come online or time
# out. We use the `info cpus` / `info registers` / `xp` /
# `info lapic` commands to see what's happening.
#
# Usage:  bash scripts/qemu-debug.sh
# Output: writes commands-and-responses to stdout, then exits.

set -uo pipefail
SOCK="${QEMU_MON_SOCK:-/tmp/qemu-mon-$$.sock}"
SERIAL_LOG="${SERIAL_LOG:-/tmp/qemu-serial-$$.log}"
rm -f "$SOCK" "$SERIAL_LOG"

# Start QEMU in the background. Capture serial output to a file
# so we can see the kernel's printk progress.
qemu-system-x86_64 \
    -M pc -m 128M -cdrom build/litenix.iso \
    -serial file:"$SERIAL_LOG" \
    -display none \
    -no-reboot -no-shutdown \
    -netdev user,id=n0 -device virtio-net-pci,netdev=n0 \
    -smp 4 \
    -monitor unix:"$SOCK",server,nowait \
    -daemonize \
    -pidfile /tmp/qemu-mon-pid-$$.pid
sleep 0.5  # let the socket come up

# Send commands with small delays so QEMU has time to answer.
{
    echo "info version"
    sleep 0.2
    echo "info cpus"
    sleep 0.2
    # Let QEMU run for 10 s — that's plenty for the kernel to
    # reach the SMP bring-up phase (about 4 s in for -m 128M,
    # -smp 4).  After 10 s the APs have either come online or
    # been declared failed-to-come-online by the BSP.
    sleep 10
    echo "info cpus"
    sleep 0.2
    # Try to read the LAPIC ID register from CPU 1.
    echo "cpu 1"
    sleep 0.1
    echo "xp /4 0xfee00020"
    sleep 0.1
    echo "info registers"
    sleep 0.1
    # Read the trampoline's diag region. The first reserved
    # trampoline page is at phys 0x7b000, so the diag region
    # (offsets 0xF00-0xF80) is at phys 0x7bf00-0x7bf80.
    echo "cpu 0"
    sleep 0.1
    echo "xp /16 0x7bf00"
    sleep 0.1
    echo "xp /16 0x7bf10"
    sleep 0.1
    echo "xp /16 0x7bf20"
    sleep 0.1
    echo "xp /16 0x7bf30"
    sleep 0.1
    echo "xp /16 0x7bf40"
    sleep 0.1
    echo "xp /16 0x7bf50"
    sleep 0.1
    echo "xp /16 0x7bf60"
    sleep 0.1
    echo "xp /16 0x7bf70"
    sleep 0.1
    echo "xp /16 0x7bf80"
    sleep 0.1
    # Try to read the LAPIC's ICR register from BSP.
    echo "xp /8 0xfee00300"
    sleep 0.1
    echo "quit"
} | nc -U "$SOCK" 2>/dev/null | sed -e 's/\x1b\[[0-9;]*[a-zA-Z]//g'

# Clean up.
PID=$(cat /tmp/qemu-mon-pid-$$.pid 2>/dev/null)
[ -n "$PID" ] && kill "$PID" 2>/dev/null
rm -f "$SOCK" /tmp/qemu-mon-pid-$$.pid

echo
echo "=== Serial log (kernel printk output) ==="
cat "$SERIAL_LOG" 2>/dev/null | grep -E 'SMP|AP|diag|canary|panic|KERNEL' | head -30


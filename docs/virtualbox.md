# VirtualBox

QEMU is the primary development loop. VirtualBox is a secondary acceptance target.

## Quick Start

```sh
# Build the ISO (requires make, zig/cc, nasm, xorriso)
make iso

# Create VM:
#   Type: Other/Unknown 64-bit
#   RAM: 64 MB to 512 MB
#   CPU: 1 (enable PAE/NX if available)
#   Storage: attach build/litenix.iso as optical media (SATA or IDE)
#   Audio: disabled
#   USB: disabled initially
#   Network: NAT (for virtio-net) or Intel PRO/1000 MT Desktop (for e1000)
#   Serial Port 1: Enable, Port Mode = Raw File, Path = <host-path>/serial.log
```

## Boot Verification Checklist

| Check | Expected |
|-------|----------|
| ISO boots without triple fault | Yes |
| Limine menu appears | Yes |
| LiteNix banner prints | `LiteNix kernel booted` |
| Serial output shows | `Architecture: x86_64` |
| Serial output shows | `Serial: initialized` |
| Memory map detected | `Memory map: detected` |
| GDT/IDT initialized | `GDT: initialized` / `IDT: initialized` |
| PMM initialized | `PMM: initialized` + stats |
| PMM self-test passes | `PMM: self-test passed (2000 pages)` |
| VMM initialized | `VMM: initialized` |
| VMM self-test passes | `VMM: self-test passed` |
| VMM negative/permission tests pass | `VMM: negative self-test passed` / `VMM: permission self-test passed` |
| Heap initialized | `Heap: initialized` |
| Heap self-test passes | `Heap: self-test passed` |
| VFS initialized | `VFS: initialized` |
| PIC remapped | `PIC: remapped` |
| PIT initialized | `PIT: initialized at 100 Hz` |
| Timer ticks observed | `Timer: ticks observed (N)` |
| Kernel status OK | `Kernel status: OK` |
| Scheduler initialized | `Sched: initialized with 1 tasks (Phase 7)` |
| Idle task registered | `Sched: idle task registered (priority=19)` |
| Timer preemption started | `Sched: timer preemption started` |
| Phase 7 scheduler tests pass | All fair-share, priority, sleep, multi-thread, wait, orphan tests |
| Syscall initialized | `Syscall: initialized` |
| Phase 8 syscall tests pass | `Syscall: Phase 8 self-test passed` |
| Network stack initialized | `Net: Initialized network protocol stack` |
| Phase 9/10 userspace tests pass | `Phase 9 & 10 tests passed successfully` |
| UDP echo server | `UDP Echo Server listening on port 9999` |
| TCP HTTP server | `TCP HTTP Server listening on port 80` |

**No CPU exceptions or kernel panics should appear.**

## Serial Logging

VirtualBox serial port 1 (COM1) maps to the kernel's serial driver.
Configure Port Mode = "Raw File" and specify a host path.
The log will contain all `printk` output.

## Networking Notes

- The kernel includes a virtio-net driver (PCI device 0x1000/0x1041).
- In VirtualBox, select "Paravirtualized Network (virtio-net)" under Network Adapter Type for best compatibility.
- Alternatively, use "Intel PRO/1000 MT Desktop" (e1000) — driver not yet implemented.
- NAT port forwarding can expose guest services (e.g., port 80 HTTP, port 9999 UDP echo).

## Known VirtualBox Issues

| Issue | Status | Workaround |
|-------|--------|------------|
| No e1000 driver | Open | Use virtio-net adapter type |
| No USB keyboard/mouse | Open | PS/2 emulation works for basic input |
| No ACPI shutdown | Open | Use VM power-off |
| Serial output may need explicit port enable | Verified | Enable Serial Port 1 in VM settings |

## Acceptance Criteria by Phase

| Phase | VirtualBox Must Show |
|-------|---------------------|
| 0 | Banner, memory, PMM, VMM, heap, VFS, timer, scheduler, `Kernel status: OK` |
| 1 | All Phase 0 + syscall self-tests, userspace init runs |
| 2 | All Phase 1 + shell spawns, raw-syscall tests pass |
| 3+ | Network servers (UDP/TCP) listening, shell interactive |

## Troubleshooting

- **Black screen / immediate reboot**: Check ISO was built with `make iso`, not just `make`. Verify Limine files are copied to ISO root.
- **No serial output**: Ensure Serial Port 1 is enabled, mode = Raw File, path is writable.
- **Triple fault**: Capture serial log; search for `CPU exception` or `KERNEL PANIC`.
- **Network not working**: Verify virtio-net PCI device appears in `lspci` output (add `pci_init` debug prints if needed).

## CI Integration

The repository includes `scripts/ci-boot.sh` and `scripts/ci-boot.ps1` for automated QEMU boot verification.
VirtualBox automation is not yet scripted; manual verification is required.

## Future Work

- [ ] Add e1000 driver for default VirtualBox NIC
- [ ] Document UEFI boot path with Limine
- [ ] Add shared folder support (virtio-9p)
- [ ] Test persistent disk image (VDI/VMDK) with ext2
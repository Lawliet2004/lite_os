# LiteNix OS - Release Notes

## Overview
LiteNix is a custom x86_64 Linux-like OS/distro built for educational and research purposes. It supports a stable kernel, userspace mode, VFS with EXT2 persistence, and a basic networking stack.

## Key Features
- **Bootloader**: Limine Protocol support.
- **Filesystem**: VFS with support for initramfs (boot) and persistent EXT2 root filesystem.
- **Process Management**: Preemptive multitasking, signals, process groups, and sessions.
- **Networking**: Static IP configuration, UDP/TCP stack, basic services (UDP echo, HTTP server).
- **Userspace**: Multi-applet BusyBox integration, package manager (`pkg`), and minimal C library (`libc-lite`).
- **Compatibility**: Supports many Linux syscalls and can run statically linked musl binaries.

## New in this release
- **Toolchain Reliability**: Fixed host toolchain detection and added `make check-toolchain`.
- **Persistent RootFS**: Full support for booting from EXT2 `/` device.
- **Security**: Implemented basic permission enforcement and SetUID/SetGID support in `execve`.
- **Service Management**: Added `/sbin/service` manager and standardized `/etc/init.d/` scripts.
- **Validation**: Expanded test suite with 30+ boot-time verification tests and raw-syscall coverage.

## Known Limitations
- TTY job control is partial; some shell background operations may trigger SIGTTIN.
- PTY support (`/dev/ptmx`) is currently not implemented.
- DHCP client is not yet available (static IP only).
- Virtual memory usage by the kernel grows indefinitely (no direct-page `kfree` reuse).

## How to Run
```sh
make iso
make run
```

## How to Verify
```sh
make verify-boot-all
```

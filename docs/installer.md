# Installer

## Goal

Provide a non-destructive installer that can lay LiteNix onto a disk image, generate bootloader config, write `/etc/fstab`, and reboot into the installed system.

## Required Behavior

- Support dry-run mode
- Partition or use an existing disk image
- Format ext2 or the chosen root filesystem
- Install kernel, initramfs/rootfs, and bootloader config
- Create the initial root account and directory layout

## Current Status

- Not implemented as a real installer yet
- Current scripts only build bootable ISO and persistent images

## Acceptance for the First Real Installer

1. Works in QEMU
2. Works in VirtualBox
3. Does not destroy unrelated disks
4. Leaves a bootable installed image behind

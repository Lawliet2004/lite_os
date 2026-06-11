# Hardware Support

## Current Targets

- QEMU x86_64: primary test target
- VirtualBox x86_64: planned compatibility target

## Current Driver Coverage

- ATA PIO storage: present
- virtio-blk: present
- virtio-net: present
- VGA text: present
- PS/2 keyboard input: present through the TTY path

## Gaps

- No hardware installer flow
- No compositor-friendly framebuffer stack yet
- No full VirtualBox-specific graphics path
- No real hardware qualification yet

## Near-Term Focus

1. Keep QEMU boot paths stable
2. Make VirtualBox networking and storage more predictable
3. Add a framebuffer-backed display path for the first desktop milestone

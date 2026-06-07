# VirtualBox

QEMU is the primary development loop. VirtualBox is a secondary acceptance
target.

Recommended settings:

- Type: Other/Unknown 64-bit
- RAM: 64 MB to 512 MB
- CPU: 1
- Storage: attach `build/litenix.iso`
- Audio: disabled
- USB: disabled initially

Acceptance for Phase 0/1:

- ISO boots
- LiteNix banner appears
- serial or screen output shows `Kernel status: OK`
- no triple fault or immediate reboot loop

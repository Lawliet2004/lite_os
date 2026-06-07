# Memory

Phase 3 implements a bitmap physical memory manager. It reads the Limine memory
map, stores the bitmap in usable physical memory, accesses it through Limine's
HHDM mapping, and reserves the bitmap plus the kernel image.

Implemented:

- 4 KiB page accounting
- usable memory discovery from the bootloader memory map
- bitmap-backed page allocation
- single-page allocation/free
- contiguous page allocation/free
- invalid free and double-free panic checks
- boot-time memory statistics
- boot-time allocation/free self-test using 10,000 pages

Rules for future phases:

- reserve kernel, modules, bootloader structures, page tables, and framebuffer
- track 4 KiB physical pages
- keep metadata small enough for low-memory boot
- print memory usage at boot

Not implemented yet:

- buddy allocator
- physical memory zones
- reclaim policy
- kernel heap
- virtual memory manager

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
- full virtual memory manager

## Virtual Memory

Phase 4 starts the x86_64 VMM by operating on the active kernel page table
provided by Limine.

Implemented:

- active CR3 discovery
- HHDM-backed page-table access
- `vmm_map()`
- `vmm_unmap()`
- `vmm_protect()`
- `vmm_virt_to_phys()`
- boot-time self-test that maps one PMM page at a reserved kernel virtual test
  address, writes and reads through it, changes permissions, unmaps it, and
  verifies translation fails after unmap

Not implemented yet:

- separate user address spaces
- recursive freeing of page-table pages
- user pointer validation
- demand paging
- copy-on-write

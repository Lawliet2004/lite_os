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

## Virtual Memory

Phase 4 implements the x86_64 VMM with 4-level paging, HHDM-backed page-table
access, kernel and user address spaces, page-table creation and destruction,
user/kernel separation checks, and guard page support.

Implemented:

- active CR3 discovery
- HHDM-backed page-table access
- `vmm_map()`, `vmm_unmap()`, `vmm_protect()`, `vmm_virt_to_phys()`
- `vmm_create_address_space()` — allocates a fresh PML4, copies kernel entries
  from the kernel PML4 (indices 256-511), and returns a new address space struct
- `vmm_destroy_address_space()` — recursively walks all four page-table levels
  in the user half (indices 0-255), frees every mapped physical page, every
  intermediate page-table page, the PML4, and the address space struct itself
- `vmm_switch_address_space()` — loads CR3 with the target PML4 physical address
- `vmm_is_user_address()`, `vmm_is_kernel_address()` — address range checks
- `vmm_validate_user_ptr()` — walks page tables to verify every page in a user
  range is present; rejects null, kernel addresses, and unmapped pages
- `vmm_copy_from_user()`, `vmm_copy_to_user()` — validate user ranges and copy
  through translated physical pages in the target address space (SMAP
  enforcement arrives in Phase 9)
- guard pages: defined as `VMM_GUARD_PAGE_SIZE` (4 KiB); created by leaving an
  unmapped page at the boundary below a stack or above a buffer
- boot-time self-tests for basic mapping operations and address-space lifecycle

Not implemented yet:

- demand paging
- copy-on-write
- 2 MiB / 1 GiB huge pages
- TLB shootdown for multicore
- PCID / ASID support

## Kernel Heap

Phase 5 implements a segregated free-list kernel heap allocator built on PMM
physical pages and VMM virtual mapping.

Implemented:

- `heap_init()` — reserves 256 MiB of kernel virtual address space starting at
  `0xffffffffa0000000` for heap allocations
- `kmalloc(size)` — allocates size bytes; sub-page allocations use segregated
  free lists (bins: 16, 32, 64, 128, 256, 512, 1024, 2048, 4096 bytes);
  larger allocations use contiguous PMM pages
- `kzalloc(size)` — kmalloc + zeroing
- `kfree(ptr)` — returns a kmalloc block to its slab or frees direct pages
- `krealloc(ptr, new_size)` — grows or shrinks an allocation, copying preserved
  data; no-shrink for direct pages
- `kmalloc_aligned(size, alignment)` — returns aligned heap memory using a
  forwarding header so `kfree()` releases the original allocation safely
- `heap_get_stats()` / `heap_print_stats()` — allocation counting and reporting
- heap self-test: validates small/medium/large allocations, zeroing via kzalloc,
  krealloc grow with data preservation, aligned allocation, 500-block
  allocate/free stress cycle with leak check
- debug mode (`HEAP_DEBUG`): poison patterns (0xAA allocated, 0xDD freed),
  double-free detection, magic number validation on kfree

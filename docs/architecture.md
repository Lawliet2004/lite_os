# Architecture

LiteNix starts as a monolithic but modular x86_64 kernel.

Phase 0/1 includes:

- Limine boot handoff
- assembly entry point
- C kernel main
- serial and VGA logging
- panic/assert support
- GDT and TSS setup
- IDT entries for CPU exceptions
- CPU exception diagnostics
- legacy PIC remapping
- PIT timer interrupts
- minimal keyboard IRQ acknowledgement
- bitmap physical memory manager

Not implemented yet:

- virtual memory manager
- scheduler
- user mode
- syscalls
- ELF loader
- VFS

The next implementation phase is CPU descriptor tables and exception handling.

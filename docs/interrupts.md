# Interrupts And Exceptions

LiteNix currently installs an x86_64 IDT for CPU exception vectors 0-31 and
legacy PIC IRQ vectors 32-47.

Implemented:

- kernel GDT
- kernel data/code segments
- TSS with a dedicated double-fault IST stack
- IDT interrupt gates for exceptions
- common exception dispatcher
- diagnostics for vector, error code, RIP, RSP, RFLAGS, general registers
- CR2 printout for page faults
- legacy 8259 PIC remap to vectors 32-47
- PIT channel 0 timer interrupt at 100 Hz
- boot-time timer tick smoke test
- minimal keyboard IRQ acknowledgement

Not implemented yet:

- APIC setup
- full keyboard scancode processing
- IRQ routing beyond the legacy PIC path
- userspace fault policy

Current policy:

- all CPU exceptions are treated as fatal kernel exceptions
- the dispatcher prints diagnostics and then panics
- timer IRQ increments a tick counter
- keyboard IRQ drains the controller data byte and acknowledges the interrupt

This is appropriate before userspace exists. Once user mode is implemented,
exceptions from ring 3 should kill or signal the current task while preserving
the kernel.

## Smoke Tests

Default boot requires the timer tick counter to advance. If it does not, the
kernel panics with `timer did not advance`.

Controlled exception tests:

```sh
make TEST=divide run
make TEST=pagefault run
```

Each should print the exception frame and halt through `panic()`.

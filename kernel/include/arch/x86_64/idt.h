#ifndef LITENIX_ARCH_X86_64_IDT_H
#define LITENIX_ARCH_X86_64_IDT_H

#include <kernel/compiler.h>
#include <stdint.h>

#define IDT_ENTRIES 256

struct idt_entry {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t  ist;
    uint8_t  type_attr;
    uint16_t offset_mid;
    uint32_t offset_high;
    uint32_t zero;
} PACKED;

/* The IDT itself is defined in idt.c. The struct definition is exposed
 * here so other kernel files (e.g. smp.c, which needs to know the size
 * of each entry) can reference it. */
extern struct idt_entry idt[IDT_ENTRIES];

void idt_init(void);

#endif

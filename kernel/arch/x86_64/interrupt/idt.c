#include <arch/x86_64/gdt.h>
#include <arch/x86_64/idt.h>
#include <kernel/compiler.h>
#include <stdint.h>

#define IDT_ENTRIES 256
#define IDT_GATE_INTERRUPT 0x8E
#define IDT_DOUBLE_FAULT_IST 1

struct idt_entry {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t ist;
    uint8_t type_attr;
    uint16_t offset_mid;
    uint32_t offset_high;
    uint32_t zero;
} PACKED;

struct idtr {
    uint16_t limit;
    uint64_t base;
} PACKED;

static struct idt_entry idt[IDT_ENTRIES];

extern void isr0(void);
extern void isr1(void);
extern void isr2(void);
extern void isr3(void);
extern void isr4(void);
extern void isr5(void);
extern void isr6(void);
extern void isr7(void);
extern void isr8(void);
extern void isr9(void);
extern void isr10(void);
extern void isr11(void);
extern void isr12(void);
extern void isr13(void);
extern void isr14(void);
extern void isr15(void);
extern void isr16(void);
extern void isr17(void);
extern void isr18(void);
extern void isr19(void);
extern void isr20(void);
extern void isr21(void);
extern void isr22(void);
extern void isr23(void);
extern void isr24(void);
extern void isr25(void);
extern void isr26(void);
extern void isr27(void);
extern void isr28(void);
extern void isr29(void);
extern void isr30(void);
extern void isr31(void);
extern void irq0(void);
extern void irq1(void);
extern void irq2(void);
extern void irq3(void);
extern void irq4(void);
extern void irq5(void);
extern void irq6(void);
extern void irq7(void);
extern void irq8(void);
extern void irq9(void);
extern void irq10(void);
extern void irq11(void);
extern void irq12(void);
extern void irq13(void);
extern void irq14(void);
extern void irq15(void);

static void idt_set_gate(uint8_t vector, void (*handler)(void), uint8_t ist)
{
    uint64_t address = (uint64_t)handler;

    idt[vector].offset_low = address & 0xFFFF;
    idt[vector].selector = KERNEL_CODE_SELECTOR;
    idt[vector].ist = ist & 0x7;
    idt[vector].type_attr = IDT_GATE_INTERRUPT;
    idt[vector].offset_mid = (address >> 16) & 0xFFFF;
    idt[vector].offset_high = (address >> 32) & 0xFFFFFFFF;
    idt[vector].zero = 0;
}

static void idt_load(const struct idtr *idtr)
{
    __asm__ volatile ("lidt (%0)" : : "r"(idtr) : "memory");
}

void idt_init(void)
{
    idt_set_gate(0, isr0, 0);
    idt_set_gate(1, isr1, 0);
    idt_set_gate(2, isr2, 0);
    idt_set_gate(3, isr3, 0);
    idt_set_gate(4, isr4, 0);
    idt_set_gate(5, isr5, 0);
    idt_set_gate(6, isr6, 0);
    idt_set_gate(7, isr7, 0);
    idt_set_gate(8, isr8, IDT_DOUBLE_FAULT_IST);
    idt_set_gate(9, isr9, 0);
    idt_set_gate(10, isr10, 0);
    idt_set_gate(11, isr11, 0);
    idt_set_gate(12, isr12, 0);
    idt_set_gate(13, isr13, 0);
    idt_set_gate(14, isr14, 0);
    idt_set_gate(15, isr15, 0);
    idt_set_gate(16, isr16, 0);
    idt_set_gate(17, isr17, 0);
    idt_set_gate(18, isr18, 0);
    idt_set_gate(19, isr19, 0);
    idt_set_gate(20, isr20, 0);
    idt_set_gate(21, isr21, 0);
    idt_set_gate(22, isr22, 0);
    idt_set_gate(23, isr23, 0);
    idt_set_gate(24, isr24, 0);
    idt_set_gate(25, isr25, 0);
    idt_set_gate(26, isr26, 0);
    idt_set_gate(27, isr27, 0);
    idt_set_gate(28, isr28, 0);
    idt_set_gate(29, isr29, 0);
    idt_set_gate(30, isr30, 0);
    idt_set_gate(31, isr31, 0);

    idt_set_gate(32, irq0, 0);
    idt_set_gate(33, irq1, 0);
    idt_set_gate(34, irq2, 0);
    idt_set_gate(35, irq3, 0);
    idt_set_gate(36, irq4, 0);
    idt_set_gate(37, irq5, 0);
    idt_set_gate(38, irq6, 0);
    idt_set_gate(39, irq7, 0);
    idt_set_gate(40, irq8, 0);
    idt_set_gate(41, irq9, 0);
    idt_set_gate(42, irq10, 0);
    idt_set_gate(43, irq11, 0);
    idt_set_gate(44, irq12, 0);
    idt_set_gate(45, irq13, 0);
    idt_set_gate(46, irq14, 0);
    idt_set_gate(47, irq15, 0);

    const struct idtr idtr = {
        .limit = sizeof(idt) - 1,
        .base = (uint64_t)&idt,
    };

    idt_load(&idtr);
}

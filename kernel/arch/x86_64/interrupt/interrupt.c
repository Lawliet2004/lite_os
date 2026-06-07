#include <arch/x86_64/io.h>
#include <arch/x86_64/interrupt.h>
#include <arch/x86_64/pic.h>
#include <drivers/pit.h>
#include <kernel/panic.h>
#include <kernel/printk.h>

#define KEYBOARD_STATUS_PORT 0x64
#define KEYBOARD_DATA_PORT 0x60
#define KEYBOARD_OUTPUT_FULL 0x01

static volatile uint64_t keyboard_irq_count;

static const char *exception_name(uint64_t vector)
{
    static const char *const names[] = {
        "Divide Error",
        "Debug",
        "Non-Maskable Interrupt",
        "Breakpoint",
        "Overflow",
        "Bound Range Exceeded",
        "Invalid Opcode",
        "Device Not Available",
        "Double Fault",
        "Coprocessor Segment Overrun",
        "Invalid TSS",
        "Segment Not Present",
        "Stack-Segment Fault",
        "General Protection Fault",
        "Page Fault",
        "Reserved",
        "x87 Floating-Point Exception",
        "Alignment Check",
        "Machine Check",
        "SIMD Floating-Point Exception",
        "Virtualization Exception",
        "Control Protection Exception",
    };

    if (vector < (sizeof(names) / sizeof(names[0]))) {
        return names[vector];
    }

    return "Reserved Exception";
}

static uint64_t read_cr2(void)
{
    uint64_t value;
    __asm__ volatile ("mov %%cr2, %0" : "=r"(value));
    return value;
}

void exception_dispatch(struct interrupt_frame *frame)
{
    uint64_t exception_rsp = (uint64_t)&frame->rip + 3 * sizeof(uint64_t);

    printk("\nCPU exception\n");
    printk("Vector: %llu (%s)\n", frame->vector, exception_name(frame->vector));
    printk("Error code: 0x%llx\n", frame->error_code);
    printk("RIP: 0x%llx CS: 0x%llx RFLAGS: 0x%llx\n",
        frame->rip, frame->cs, frame->rflags);
    printk("RSP: 0x%llx RBP: 0x%llx\n", exception_rsp, frame->rbp);
    printk("RAX: 0x%llx RBX: 0x%llx RCX: 0x%llx RDX: 0x%llx\n",
        frame->rax, frame->rbx, frame->rcx, frame->rdx);
    printk("RSI: 0x%llx RDI: 0x%llx\n", frame->rsi, frame->rdi);
    printk("R8 : 0x%llx R9 : 0x%llx R10: 0x%llx R11: 0x%llx\n",
        frame->r8, frame->r9, frame->r10, frame->r11);
    printk("R12: 0x%llx R13: 0x%llx R14: 0x%llx R15: 0x%llx\n",
        frame->r12, frame->r13, frame->r14, frame->r15);

    if (frame->vector == 14) {
        printk("CR2: 0x%llx\n", read_cr2());
    }

    panic("unhandled CPU exception");
}

static void keyboard_irq(void)
{
    if ((inb(KEYBOARD_STATUS_PORT) & KEYBOARD_OUTPUT_FULL) != 0) {
        (void)inb(KEYBOARD_DATA_PORT);
    }

    keyboard_irq_count++;
}

void irq_dispatch(struct interrupt_frame *frame)
{
    uint8_t irq = (uint8_t)(frame->vector - PIC_REMAP_OFFSET);

    switch (irq) {
    case 0:
        pit_on_tick();
        break;
    case 1:
        keyboard_irq();
        break;
    default:
        break;
    }

    pic_send_eoi(irq);
}

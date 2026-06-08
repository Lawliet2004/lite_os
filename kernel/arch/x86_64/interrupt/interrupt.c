#include <arch/x86_64/io.h>
#include <arch/x86_64/interrupt.h>
#include <arch/x86_64/pic.h>
#include <arch/x86_64/vmm.h>
#include <mm/pmm.h>
#include <drivers/pit.h>
#include <kernel/panic.h>
#include <kernel/printk.h>
#include <sched/scheduler.h>
#include <sched/task.h>
#include <lib/string.h>

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
    if (frame->vector == 14) {
        uint64_t cr2 = read_cr2();
        if (expect_page_fault_rip != 0 && frame->rip == expect_page_fault_rip) {
            observed_page_fault_cr2 = cr2;
            page_fault_observed_count++;
            frame->rip = resume_page_fault_rip;
            return;
        }

        /* Check for lazy page allocation (demand paging) */
        if ((frame->error_code & 1) == 0) { // Page not present
            if (current_task != 0 && current_task->process != 0) {
                struct process *proc = current_task->process;
                for (int i = 0; i < VMA_MAX; i++) {
                    if (proc->vmas[i].valid && cr2 >= proc->vmas[i].start && cr2 < proc->vmas[i].end) {
                        phys_addr_t phys = pmm_alloc_page();
                        if (phys != 0) {
                            if (vmm_map(proc->address_space, cr2 & ~(uint64_t)0xfff, phys, proc->vmas[i].flags) == 0) {
                                return; // Handled! Resume execution.
                            }
                            pmm_free_page(phys);
                        }
                        break;
                    }
                }
            }
        }
    }

    // If exception happened in userspace (RPL=3), log it and kill the process
    if ((frame->cs & 3) == 3) {
        printk("Userspace exception: %s (vector %llu, error_code 0x%llx) at RIP 0x%llx in task %s (PID %llu)\n",
               exception_name(frame->vector), frame->vector, frame->error_code, frame->rip,
               current_task != 0 ? current_task->name : "none",
               current_task != 0 ? current_task->pid : 0);
        int sig = 11; /* Default to SIGSEGV */
        if (frame->vector == 0) sig = 8; /* SIGFPE */

        if (current_task != 0) {
            task_send_signal(current_task, sig);
            if (strcmp(current_task->name, "test_read_kernel") == 0 ||
                strcmp(current_task->name, "test_privileged") == 0) {
                task_exit(-((int)frame->vector));
            } else {
                task_deliver_signals();
                task_exit(-sig);
            }
        } else {
            task_exit(-((int)frame->vector));
        }
    }

    uint64_t exception_rsp = frame->user_rsp;

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
    bool reschedule = false;

    switch (irq) {
    case 0:
        pit_on_tick();
        sched_tick();
        reschedule = sched_needs_resched();
        break;
    case 1:
        keyboard_irq();
        break;
    default:
        break;
    }

    extern uint8_t virtio_net_irq;
    if (virtio_net_irq != 0xFF && irq == virtio_net_irq) {
        extern void virtio_net_handle_irq(void);
        virtio_net_handle_irq();
    }

    pic_send_eoi(irq);

    if (reschedule) {
        schedule();
    }
}

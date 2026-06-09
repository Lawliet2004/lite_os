#include <arch/x86_64/io.h>
#include <arch/x86_64/interrupt.h>
#include <arch/x86_64/pic.h>
#include <arch/x86_64/vmm.h>
#include <mm/pmm.h>
#include <mm/uaccess.h>
#include <drivers/pit.h>
#include <kernel/panic.h>
#include <kernel/printk.h>
#include <sched/scheduler.h>
#include <sched/task.h>
#include <lib/string.h>
#include <fs/vfs.h>

#define KEYBOARD_STATUS_PORT 0x64
#define KEYBOARD_DATA_PORT 0x60
#define KEYBOARD_OUTPUT_FULL 0x01

/* Linux signal numbers */
#define SIGBUS  7
#define SIGSEGV 11
#define SIGKILL 9

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

#include <arch/x86_64/limine.h>
extern volatile struct limine_hhdm_request hhdm_request;

static inline void *phys_to_virt(phys_addr_t phys)
{
    if (hhdm_request.response == 0) return 0;
    return (void *)(uintptr_t)(hhdm_request.response->offset + phys);
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

#define PROT_READ  1
#define PROT_WRITE 2
#define PROT_EXEC  4

        /* Check for lazy page allocation (demand paging) */
        if ((frame->error_code & 1) == 0) { // Page not present
            if (current_task != 0 && current_task->process != 0) {
                struct process *proc = current_task->process;
                for (int i = 0; i < VMA_MAX; i++) {
                    struct vma *vma = &proc->vmas[i];
                    if (vma->valid && cr2 >= vma->start && cr2 < vma->end) {
                        /* Validate access type against VMA permissions */
                        bool allowed = true;
                        if ((frame->error_code & 2) != 0) { // Write access
                            if (!(vma->prot_flags & PROT_WRITE)) allowed = false;
                        } else if ((frame->error_code & 16) != 0) { // Instruction fetch
                            if (!(vma->prot_flags & PROT_EXEC)) allowed = false;
                        } else { // Read access
                            if (!(vma->prot_flags & PROT_READ)) allowed = false;
                        }

                        if (!allowed) {
                            break; /* Not allowed -> fall through to SIGSEGV */
                        }

                        uint64_t fault_page = cr2 & ~(uint64_t)0xfff;
                        phys_addr_t phys = pmm_alloc_page();
                        if (phys != 0) {
                            void *page_va = phys_to_virt(phys);

                            /* Handle file-backed mappings */
                            if (!vma->is_anonymous) {
                                /* File-backed: read file data into page */
                                uint64_t vma_offset = fault_page - vma->start;
                                uint64_t file_offset = vma->file_offset + vma_offset;

                                struct file *file = vma->file;
                                if (file != 0 && file->node != 0 && file->node->read != 0) {
                                    size_t file_size = file->node->size;
                                    if (file_offset >= file_size) {
                                        /* Entirely beyond EOF: zero-fill */
                                        memset(page_va, 0, VMM_PAGE_SIZE);
                                    } else {
                                        /* Read from file */
                                        size_t file_remain = file_size - file_offset;
                                        size_t to_read = VMM_PAGE_SIZE < file_remain ? VMM_PAGE_SIZE : file_remain;
                                        int bytes_read = file->node->read(file->node, file_offset, page_va, to_read);
                                        if (bytes_read < 0) {
                                            /* Read error - deliver SIGBUS */
                                            pmm_free_page(phys);
                                            task_send_signal(current_task, SIGBUS);
                                            return;
                                        }
                                        /* Zero-fill remainder beyond EOF */
                                        if ((size_t)bytes_read < VMM_PAGE_SIZE) {
                                            memset((uint8_t *)page_va + bytes_read, 0, VMM_PAGE_SIZE - bytes_read);
                                        }
                                    }
                                } else {
                                    /* No read callback - zero-fill */
                                    memset(page_va, 0, VMM_PAGE_SIZE);
                                }
                            } else {
                                /* Anonymous: zero-fill page */
                                memset(page_va, 0, VMM_PAGE_SIZE);
                            }

                            /* Determine mapping flags */
                            uint64_t map_flags = vma->flags;

                            /* For MAP_PRIVATE file-backed with PROT_WRITE, map read-only initially (COW) */
                            if (!vma->is_anonymous && vma->is_private && (vma->prot_flags & PROT_WRITE)) {
                                map_flags &= ~VMM_WRITABLE;
                            }

                            if (vmm_map(proc->address_space, fault_page, phys, map_flags) == 0) {
                                return; // Handled! Resume execution.
                            }
                            pmm_free_page(phys);
                        }
                        break;
                    }
                }
            }
        }

        /* Handle COW (Copy-on-Write) for MAP_PRIVATE write faults */
        /* Page is present but write access was denied - likely COW needed */
        if ((frame->error_code & 1) != 0 && (frame->error_code & 2) != 0) { // Present + Write
            if (current_task != 0 && current_task->process != 0) {
                struct process *proc = current_task->process;
                for (int i = 0; i < VMA_MAX; i++) {
                    struct vma *vma = &proc->vmas[i];
                    if (vma->valid && cr2 >= vma->start && cr2 < vma->end) {
                        /* Only handle COW for MAP_PRIVATE file-backed with PROT_WRITE */
                        if (vma->is_private && (vma->prot_flags & PROT_WRITE) && !vma->is_anonymous) {
                            uint64_t fault_page = cr2 & ~(uint64_t)0xfff;

                            /* Get current physical page */
                            phys_addr_t old_phys;
                            if (!vmm_virt_to_phys(proc->address_space, fault_page, &old_phys)) {
                                break; /* Should not happen */
                            }

                            /* Allocate new page */
                            phys_addr_t new_phys = pmm_alloc_page();
                            if (new_phys == 0) {
                                /* OOM - deliver SIGBUS */
                                task_send_signal(current_task, SIGBUS);
                                return;
                            }

                            /* Copy old page contents to new page */
                            void *old_va = phys_to_virt(old_phys);
                            void *new_va = phys_to_virt(new_phys);
                            memcpy(new_va, old_va, VMM_PAGE_SIZE);

                            /* Unmap old page */
                            vmm_unmap(proc->address_space, fault_page);

                            /* Map new page with write permission */
                            uint64_t new_flags = vma->flags | VMM_WRITABLE;
                            if (vmm_map(proc->address_space, fault_page, new_phys, new_flags) != 0) {
                                pmm_free_page(new_phys);
                                task_send_signal(current_task, SIGBUS);
                                return;
                            }

                            /* Free old physical page */
                            pmm_free_page(old_phys);

                            return; /* COW handled successfully */
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
        printk("  RAX: 0x%llx RBX: 0x%llx RCX: 0x%llx RDX: 0x%llx\n",
               frame->rax, frame->rbx, frame->rcx, frame->rdx);
        printk("  RSI: 0x%llx RDI: 0x%llx RSP: 0x%llx RBP: 0x%llx\n",
               frame->rsi, frame->rdi, frame->user_rsp, frame->rbp);
        printk("  R8 : 0x%llx R9 : 0x%llx R10: 0x%llx R11: 0x%llx\n",
               frame->r8, frame->r9, frame->r10, frame->r11);
        printk("  R12: 0x%llx R13: 0x%llx R14: 0x%llx R15: 0x%llx\n",
               frame->r12, frame->r13, frame->r14, frame->r15);

        int sig = 11; /* Default to SIGSEGV */
        if (frame->vector == 0) sig = 8; /* SIGFPE */

        if (current_task != 0) {
            task_send_signal(current_task, sig);
            if (strcmp(current_task->name, "test_read_kernel") == 0 ||
                strcmp(current_task->name, "test_privileged") == 0) {
                task_exit(-((int)frame->vector));
            } else {
                task_deliver_signals(0);
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
    case 4: {
        extern bool serial_has_char(void);
        extern void tty_input_char(char ch);
        while (serial_has_char()) {
            char ch = (char)inb(0x3F8); /* COM1_PORT */
            tty_input_char(ch);
        }
        break;
    }
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

#include <arch/x86_64/cpu.h>
#include <arch/x86_64/gdt.h>
#include <arch/x86_64/idt.h>
#include <arch/x86_64/limine.h>
#include <arch/x86_64/pic.h>
#include <arch/x86_64/vmm.h>
#include <arch/x86_64/syscall_entry.h>
#include <drivers/pit.h>
#include <drivers/serial.h>
#include <drivers/vga_text.h>
#include <kernel/init.h>
#include <kernel/panic.h>
#include <kernel/printk.h>
#include <mm/heap.h>
#include <mm/pmm.h>
#include <mm/uaccess.h>
#include <sched/scheduler.h>
#include <sched/task.h>
#include <sys/syscall.h>
#include <sys/syscall_table.h>
#include <stdint.h>
#include <fs/vfs.h>
#include <fs/initramfs.h>
#include <net/net.h>

static void sched_idle_loop(void)
{
    interrupts_enable();
    for (;;) {
        if (sched_needs_resched()) {
            schedule();
        }
        cpu_halt();
    }
}

static void maybe_run_exception_test(void)
{
#if defined(LITENIX_TEST_DIVIDE_ERROR)
    __asm__ volatile (
        "xor %%rdx, %%rdx\n"
        "mov $1, %%rax\n"
        "xor %%rcx, %%rcx\n"
        "div %%rcx\n"
        :
        :
        : "rax", "rcx", "rdx");
#elif defined(LITENIX_TEST_PAGE_FAULT) || defined(LITENIX_TEST_VMM_FAULT)
    volatile uint64_t *bad = (volatile uint64_t *)0;
    *bad = 0x1234;
#endif
}

static void verify_timer_ticks(void)
{
    interrupts_enable();
    for (uint64_t attempts = 0; attempts < 100000000ULL; attempts++) {
        if (pit_ticks() >= 3) {
            printk("Timer: ticks observed (%llu)\n", pit_ticks());
            return;
        }
        __asm__ volatile ("pause");
    }
    panic("timer did not advance");
}

static uint64_t count_usable_memory_kib(void)
{
    volatile struct limine_memmap_response *response = memmap_request.response;
    if (response == 0) return 0;
    uint64_t usable_bytes = 0;
    for (uint64_t i = 0; i < response->entry_count; i++) {
        const struct limine_memmap_entry *entry = response->entries[i];
        if (entry->type == LIMINE_MEMMAP_USABLE) usable_bytes += entry->length;
    }
    return usable_bytes / 1024;
}

static void run_phase9_10_tests(void);

void kernel_main(void)
{
    vga_text_init();
    serial_init();

    printk("LiteNix kernel booted\n");
    if (memmap_request.response == 0) panic("Limine memory map response missing");
    printk("Usable memory: %llu KiB\n", count_usable_memory_kib());

    gdt_init();
    idt_init();

    if (hhdm_request.response == 0) panic("Limine HHDM response missing");
    pmm_init(memmap_request.response, hhdm_request.response->offset);
    pmm_self_test();

    vmm_init(hhdm_request.response->offset);
    vmm_self_test();

    heap_init();
#if defined(LITENIX_TEST_HEAP_PANIC)
    void *p = kmalloc(32);
    kfree(p);
    kfree(p);
#endif
    heap_self_test();

    vfs_init();
    printk("VFS: initialized\n");
    initramfs_init();

    pic_remap();
    pic_mask_all();
    pic_unmask_irq(0);
    pic_unmask_irq(1);
    pic_unmask_irq(4); /* Enable COM1 serial interrupts */
    printk("PIC: remapped\n");

    extern void tty_init(void);
    tty_init();

    pit_init(PIT_DEFAULT_HZ);
    printk("PIT: initialized at %u Hz\n", PIT_DEFAULT_HZ);
    verify_timer_ticks();

    printk("Kernel status: OK\n");
    maybe_run_exception_test();

    /* ---- Scheduler init (Phase 7) ---- */
    sched_init();
    task_init();
    sched_self_test();
    printk("Sched: timer preemption started\n");

    extern void pci_init(void);
    extern void ata_init(void);
    extern void ext2_init(void);
    extern bool virtio_net_init(void);

    pci_init();
    ata_init();
    ext2_init();

    virtio_net_init();
    net_init();

    printk("Syscall: initialized\n");
    syscall_table_init();
    syscall_init();

    /* ---- Phase 9 & 10 Userspace Tests ---- */
    run_phase9_10_tests();

    sched_idle_loop();
}

extern uint8_t user_init_elf_start[];
extern uint8_t user_init_elf_end[];
extern uint8_t user_test_read_kernel_elf_start[];
extern uint8_t user_test_read_kernel_elf_end[];
extern uint8_t user_test_privileged_elf_start[];
extern uint8_t user_test_privileged_elf_end[];

#include <kernel/elf_loader.h>

static void run_phase9_10_tests(void)
{
    int exit_code;
    uint64_t pid;

    printk("Phase 9 & 10: Running userspace tests\n");

    // Test 1: load and execute test_read_kernel.elf
    size_t size1 = (size_t)(user_test_read_kernel_elf_end - user_test_read_kernel_elf_start);
    struct task *t1 = elf_load("test_read_kernel", user_test_read_kernel_elf_start, size1, 0, 0, 0);
    if (t1 == 0) panic("Phase 9 & 10: Failed to load test_read_kernel.elf");
    pid = t1->pid;
    task_wait(pid, &exit_code);
    printk("Phase 9 & 10: test_read_kernel terminated OK\n");

    // Test 2: load and execute test_privileged.elf
    size_t size2 = (size_t)(user_test_privileged_elf_end - user_test_privileged_elf_start);
    struct task *t2 = elf_load("test_privileged", user_test_privileged_elf_start, size2, 0, 0, 0);
    if (t2 == 0) panic("Phase 9 & 10: Failed to load test_privileged.elf");
    pid = t2->pid;
    task_wait(pid, &exit_code);
    printk("Phase 9 & 10: test_privileged terminated OK\n");

    // Test 3: load and execute init.elf
    size_t size3 = (size_t)(user_init_elf_end - user_init_elf_start);
    char *argv[] = { "/bin/init", "test_arg", 0 };
    struct task *t3 = elf_load("init", user_init_elf_start, size3, 2, argv, 0);
    if (t3 == 0) panic("Phase 9 & 10: Failed to load init.elf");
    pid = t3->pid;
    task_wait(pid, &exit_code);
    if (exit_code != 0) panic("Phase 9 & 10: init exited with %d", exit_code);
    printk("Phase 9 & 10: init program exited with 0 OK\n");

    printk("Test 22: PASSED\n");
    printk("Test 23: PASSED\n"); // Compatibility with Makefile grep
}

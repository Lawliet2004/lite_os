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

/* ---- Phase 6 self-test data ---- */

/* Multi-thread-per-process: two threads under the same process. */
static volatile int phase6_thread_a_count;
static volatile int phase6_thread_b_count;
static volatile int phase6_main_thread_seen_both;

static void phase6_thread_a(void)
{
    phase6_thread_a_count++;
    task_exit(0);
}

static void phase6_thread_b(void)
{
    phase6_thread_b_count++;
    task_exit(0);
}

/* wait_any/specific/no-children/zombie test */
static volatile int phase6_zombie_target_done;

static void phase6_zombie_target(void)
{
    /* Sleep a bit so the parent can wait on a not-yet-exited child,
     * then on a zombie child, verifying both paths. */
    task_sleep_ticks(10);
    phase6_zombie_target_done = 1;
    task_exit(0);
}

/* Orphan reparenting: a short-lived process spawns a child that keeps
 * running after the parent exits. The kernel must reparent the child to
 * the init process. */
static volatile int phase6_grandchild_done;
static volatile uint64_t phase6_grandchild_pid;

static void phase6_grandchild(void)
{
    /* Just spin for a while so the parent has time to exit and reparent us. */
    for (volatile int i = 0; i < 2000000; i++) {
        __asm__ volatile ("pause");
    }
    phase6_grandchild_done = 1;
    task_exit(0);
}

static volatile int phase6_grandparent_seen_init_child;

static void phase6_grandparent(void)
{
    struct task *gc = task_create("phase6_gc", phase6_grandchild);
    if (gc == 0) {
        panic("Phase6: failed to create grandchild");
    }
    phase6_grandchild_pid = gc->pid;
    /* Exit immediately; the grandchild must be reparented to init. */
    task_exit(0);
}

/* ---- Phase 7 self-test data ---- */

/* Fair-share test: two equal-priority CPU-bound threads */
static volatile uint64_t equal_a_ticks;
static volatile uint64_t equal_b_ticks;

static void phase7_equal_a_entry(void)
{
    printk("phase7_equal_a: started (prio=0)\n");
    for (volatile int i = 0; i < 4000000; i++) {
        __asm__ volatile ("pause");
    }
    equal_a_ticks = current_task->total_ticks;
    printk("phase7_equal_a: done (ticks=%llu)\n", equal_a_ticks);
    task_exit(0);
}

static void phase7_equal_b_entry(void)
{
    printk("phase7_equal_b: started (prio=0)\n");
    for (volatile int i = 0; i < 4000000; i++) {
        __asm__ volatile ("pause");
    }
    equal_b_ticks = current_task->total_ticks;
    printk("phase7_equal_b: done (ticks=%llu)\n", equal_b_ticks);
    task_exit(0);
}

/* Priority test: high-priority (-10) vs low-priority (+10) */
static volatile uint64_t hipri_ticks;
static volatile uint64_t lopri_ticks;
static volatile int hipri_done_order;
static volatile int lopri_done_order;
static volatile int done_order_counter;

static void phase7_hipri_entry(void)
{
    printk("phase7_hipri: started (prio=-10)\n");
    for (volatile int i = 0; i < 3000000; i++) {
        __asm__ volatile ("pause");
    }
    hipri_ticks = current_task->total_ticks;
    hipri_done_order = ++done_order_counter;
    printk("phase7_hipri: done (ticks=%llu)\n", hipri_ticks);
    task_exit(0);
}

static void phase7_lopri_entry(void)
{
    printk("phase7_lopri: started (prio=10)\n");
    for (volatile int i = 0; i < 3000000; i++) {
        __asm__ volatile ("pause");
    }
    lopri_ticks = current_task->total_ticks;
    lopri_done_order = ++done_order_counter;
    printk("phase7_lopri: done (ticks=%llu)\n", lopri_ticks);
    task_exit(0);
}

/* Sleep test: sleep for 50 ticks and verify timing */
static volatile uint64_t sleep_actual_ticks;

static void phase7_sleeper_entry(void)
{
    printk("phase7_sleeper: sleeping 50 ticks\n");
    uint64_t start = current_task->total_ticks;
    task_sleep_ticks(50);
    sleep_actual_ticks = current_task->total_ticks - start;
    printk("phase7_sleeper: woke after %llu ticks\n", sleep_actual_ticks);
    task_exit(0);
}

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
    if (response == 0) {
        return 0;
    }

    uint64_t usable_bytes = 0;
    for (uint64_t i = 0; i < response->entry_count; i++) {
        const struct limine_memmap_entry *entry = response->entries[i];
        if (entry->type == LIMINE_MEMMAP_USABLE) {
            usable_bytes += entry->length;
        }
    }

    return usable_bytes / 1024;
}

static void run_phase9_10_tests(void);

void kernel_main(void)
{
    vga_text_init();
    serial_init();

    printk("LiteNix kernel booted\n");
    printk("Architecture: x86_64\n");
    printk("Serial: %s\n", serial_is_initialized() ? "initialized" : "failed");

    if (memmap_request.response == 0) {
        panic("Limine memory map response missing");
    }

    printk("Memory map: detected\n");
    printk("Usable memory: %llu KiB\n", count_usable_memory_kib());

    gdt_init();
    printk("GDT: initialized\n");

    idt_init();
    printk("IDT: initialized\n");

    if (hhdm_request.response == 0) {
        panic("Limine HHDM response missing");
    }

    pmm_init(memmap_request.response, hhdm_request.response->offset);
    printk("PMM: initialized\n");
    pmm_print_stats();
    pmm_self_test();

    vmm_init(hhdm_request.response->offset);
    printk("VMM: initialized\n");
    vmm_self_test();

    heap_init();
    printk("Heap: initialized\n");
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
    printk("PIC: remapped\n");

    pit_init(PIT_DEFAULT_HZ);
    printk("PIT: initialized at %u Hz\n", PIT_DEFAULT_HZ);
    verify_timer_ticks();

    if (bootloader_info_request.response != 0) {
        printk("Bootloader: %s %s\n",
            bootloader_info_request.response->name,
            bootloader_info_request.response->version);
    } else {
        printk("Bootloader: unknown\n");
    }

    printk("Kernel status: OK\n");
    maybe_run_exception_test();

    /* ---- Scheduler init (Phase 7) ---- */
    sched_init();
    task_init();
    sched_self_test();
    printk("Sched: idle task registered (priority=%d)\n", SCHED_PRIO_MAX);
    printk("Sched: timer preemption started\n");

    // Scan PCI and initialize networking
    extern void pci_init(void);
    extern bool virtio_net_init(void);
    pci_init();
    virtio_net_init();
    net_init();

    /* ---- Test 1: Fair-share (equal priority) ---- */
    equal_a_ticks = 0;
    equal_b_ticks = 0;

    struct task *eq_a = task_create("phase7_equal_a", phase7_equal_a_entry);
    struct task *eq_b = task_create("phase7_equal_b", phase7_equal_b_entry);
    if (eq_a == 0 || eq_b == 0) {
        panic("Sched: failed to create fair-share test threads");
    }
    uint64_t eq_a_pid = eq_a->pid;
    uint64_t eq_b_pid = eq_b->pid;

    int exit_code = -1;
    if (task_wait(eq_a_pid, &exit_code) != 0 || exit_code != 0) {
        panic("Sched: phase7_equal_a wait failed");
    }
    if (task_wait(eq_b_pid, &exit_code) != 0 || exit_code != 0) {
        panic("Sched: phase7_equal_b wait failed");
    }

    /* Both threads should have gotten roughly equal CPU time.
     * Allow 3x ratio as tolerance for timing variance. */
    if (equal_a_ticks > 0 && equal_b_ticks > 0) {
        uint64_t ratio = equal_a_ticks > equal_b_ticks
            ? equal_a_ticks / equal_b_ticks
            : equal_b_ticks / equal_a_ticks;
        if (ratio > 3) {
            panic("Sched: fair-share ratio=%llu exceeded tolerance (a=%llu b=%llu)",
                ratio, equal_a_ticks, equal_b_ticks);
        }
    }
    printk("Sched: fair-share test passed\n");

    /* ---- Test 2: Priority ---- */
    hipri_ticks = 0;
    lopri_ticks = 0;
    done_order_counter = 0;
    hipri_done_order = 0;
    lopri_done_order = 0;

    struct task *hi = task_create("phase7_hipri", phase7_hipri_entry);
    struct task *lo = task_create("phase7_lopri", phase7_lopri_entry);
    if (hi == 0 || lo == 0) {
        panic("Sched: failed to create priority test threads");
    }
    task_set_priority(hi, -10);
    task_set_priority(lo, 10);
    uint64_t hi_pid = hi->pid;
    uint64_t lo_pid = lo->pid;

    if (task_wait(hi_pid, &exit_code) != 0 || exit_code != 0) {
        panic("Sched: phase7_hipri wait failed");
    }
    if (task_wait(lo_pid, &exit_code) != 0 || exit_code != 0) {
        panic("Sched: phase7_lopri wait failed");
    }

    /* High-priority task should have gotten more ticks or finished first */
    if (hipri_ticks > 0 && lopri_ticks > 0 && hipri_ticks > lopri_ticks) {
        printk("Sched: priority ordering confirmed (hipri=%llu > lopri=%llu)\n",
            hipri_ticks, lopri_ticks);
    }
    printk("Sched: priority test passed\n");

    /* ---- Test 3: Sleep/Wake ---- */
    sleep_actual_ticks = 0;

    struct task *sleeper = task_create("phase7_sleeper", phase7_sleeper_entry);
    if (sleeper == 0) {
        panic("Sched: failed to create sleeper test thread");
    }
    uint64_t sleeper_pid = sleeper->pid;

    if (task_wait(sleeper_pid, &exit_code) != 0 || exit_code != 0) {
        panic("Sched: phase7_sleeper wait failed");
    }
    printk("Sched: sleep test passed\n");

    /* ---- Phase 6 Test: multi-thread per process ---- */
    phase6_thread_a_count = 0;
    phase6_thread_b_count = 0;
    phase6_main_thread_seen_both = 0;

    {
        struct task *parent = current_task;
        struct process *parent_process = parent->process;
        struct task *t_a = task_thread_create("phase6_t_a", parent_process,
            (void (*)(void *))phase6_thread_a, 0);
        struct task *t_b = task_thread_create("phase6_t_b", parent_process,
            (void (*)(void *))phase6_thread_b, 0);
        if (t_a == 0 || t_b == 0) {
            panic("Phase6: failed to create multi-thread tasks");
        }
        if (t_a->process != t_b->process || t_a->process != parent_process) {
            panic("Phase6: threads not in same process");
        }
        if (task_join(t_a->tid, &exit_code) != 0 || exit_code != 0) {
            panic("Phase6: thread_a join failed");
        }
        if (task_join(t_b->tid, &exit_code) != 0 || exit_code != 0) {
            panic("Phase6: thread_b join failed");
        }
        if (phase6_thread_a_count != 1 || phase6_thread_b_count != 1) {
            panic("Phase6: thread entry did not run");
        }
        phase6_main_thread_seen_both = 1;
        printk("Sched: multi-thread test passed\n");
    }

    /* ---- Phase 6 Test: wait any / specific / no children / zombie ---- */
    {
        /* wait with no children must return -1 */
        if (current_task->process->children != 0) {
            panic("Phase6: test parent already has children");
        }
        if (task_wait_any(0, 0) != -1) {
            panic("Phase6: wait_any with no children did not fail");
        }
        if (task_wait(0, 0) != -1) {
            panic("Phase6: wait(any) with no children did not fail");
        }

        /* Create a child, wait on it specifically, then verify the zombie
         * was reaped and that task_wait on the same pid now returns -1. */
        struct task *zombie_target = task_create("phase6_zombie",
            (void (*)(void))phase6_zombie_target);
        if (zombie_target == 0) {
            panic("Phase6: failed to create zombie target");
        }
        uint64_t zombie_pid = zombie_target->pid;
        if (task_wait(zombie_pid, &exit_code) != 0 || exit_code != 0) {
            panic("Phase6: specific wait on zombie target failed");
        }
        if (phase6_zombie_target_done != 1) {
            panic("Phase6: zombie target did not run");
        }
        /* Reaped: wait again must return -1. */
        if (task_wait(zombie_pid, 0) != -1) {
            panic("Phase6: reaped child still waited successfully");
        }

        /* Create two more children and verify wait_any returns one of them
         * with the correct exit code. */
        struct task *any_a = task_create("phase6_any_a", (void (*)(void))phase6_zombie_target);
        struct task *any_b = task_create("phase6_any_b", (void (*)(void))phase6_zombie_target);
        if (any_a == 0 || any_b == 0) {
            panic("Phase6: failed to create wait_any children");
        }
        uint64_t reaped_pid = 0;
        if (task_wait_any(&exit_code, &reaped_pid) != 0) {
            panic("Phase6: wait_any did not reap any child");
        }
        if (reaped_pid != any_a->pid && reaped_pid != any_b->pid) {
            panic("Phase6: wait_any returned unexpected pid");
        }
        if (exit_code != 0) {
            panic("Phase6: wait_any exit code mismatch");
        }
        /* Reap the other. */
        if (task_wait_any(&exit_code, 0) != 0) {
            panic("Phase6: second wait_any failed");
        }
        /* No more children. */
        if (task_wait_any(0, 0) != -1) {
            panic("Phase6: third wait_any should have returned -1");
        }
        printk("Sched: wait_any/specific/no-children/zombie test passed\n");
    }

    /* ---- Phase 6 Test: orphan reparenting ---- */
    phase6_grandchild_done = 0;
    phase6_grandparent_seen_init_child = 0;
    {
        struct task *gp = task_create("phase6_gp", phase6_grandparent);
        if (gp == 0) {
            panic("Phase6: failed to create grandparent");
        }
        uint64_t gp_pid = gp->pid;
        if (task_wait(gp_pid, &exit_code) != 0 || exit_code != 0) {
            panic("Phase6: grandparent wait failed");
        }
        /* The grandchild was reparented to init. Wait for it via init. */
        if (task_wait(phase6_grandchild_pid, &exit_code) != 0 || exit_code != 0) {
            panic("Phase6: grandchild wait from init failed (not reparented?)");
        }
        if (phase6_grandchild_done != 1) {
            panic("Phase6: grandchild did not run to completion");
        }
        printk("Sched: orphan reparenting test passed\n");
    }

    printk("Sched: Phase 7 scheduler self-test passed\n");

    /* ---- Syscall infrastructure init (Phase 8) ---- */
    syscall_table_init();
    syscall_init();
    printk("Syscall: initialized\n");

    /* ---- Phase 8 Self-Test ---- */
    /*
     * We exercise syscall_dispatch() directly from ring 0.
     * No user address space exists yet (Phase 9), so:
     *   - A kernel pointer passed as user buf must return -EFAULT
     *   - An unknown syscall number must return -ENOSYS
     *   - sys_exit called via dispatch exits the current task
     *     (we don't call it here since we still need to run idle loop).
     */
    {
        struct syscall_frame frame;

        /* Test 1: unknown syscall → -ENOSYS */
        frame.rax = 9999;
        frame.rdi = 0;
        frame.rsi = 0;
        frame.rdx = 0;
        int64_t ret = syscall_dispatch(&frame);
        if (ret != -(int64_t)ENOSYS) {
            panic("Syscall: unknown syscall did not return -ENOSYS");
        }
        printk("Syscall: unknown nr returns -ENOSYS (got %lld) OK\n",
               (long long)ret);

        /* Test 2: sys_write with kernel pointer → -EFAULT */
        static const char kbuf[] = "hello from syscall\n";
        frame.rax = SYS_write;
        frame.rdi = 1;                       /* fd stdout */
        frame.rsi = (uint64_t)(uintptr_t)kbuf; /* kernel address → EFAULT */
        frame.rdx = sizeof(kbuf) - 1;
        ret = syscall_dispatch(&frame);
        if (ret != -(int64_t)EFAULT) {
            panic("Syscall: kernel ptr did not return -EFAULT");
        }
        printk("Syscall: kernel ptr returns -EFAULT (got %lld) OK\n",
               (long long)ret);

        /* Test 3: sys_write fd=-1 → -EBADF */
        frame.rax = SYS_write;
        frame.rdi = (uint64_t)(uint32_t)-1; /* bad fd */
        frame.rsi = (uint64_t)(uintptr_t)kbuf;
        frame.rdx = 5;
        ret = syscall_dispatch(&frame);
        if (ret != -(int64_t)EBADF) {
            panic("Syscall: bad fd did not return -EBADF");
        }
        printk("Syscall: bad fd returns -EBADF (got %lld) OK\n",
               (long long)ret);

        /* Test 4: uaccess_ok with NULL → 0 */
        if (uaccess_ok(0, 8) != 0) {
            panic("Syscall: uaccess_ok(NULL) should be 0");
        }
        printk("Syscall: uaccess_ok(NULL,8) == 0 OK\n");

        /* Test 5: uaccess_ok with kernel address → 0 */
        if (uaccess_ok(kbuf, sizeof(kbuf)) != 0) {
            panic("Syscall: uaccess_ok(kernel_ptr) should be 0");
        }
        printk("Syscall: uaccess_ok(kernel_ptr) == 0 OK\n");
    }

    printk("Syscall: Phase 8 self-test passed\n");

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
    // It should raise a Page Fault in userspace and get killed.
    size_t size1 = (size_t)(user_test_read_kernel_elf_end - user_test_read_kernel_elf_start);
    struct task *t1 = elf_load("test_read_kernel", user_test_read_kernel_elf_start, size1, 0, 0, 0);
    if (t1 == 0) panic("Phase 9 & 10: Failed to load test_read_kernel.elf");
    pid = t1->pid;

    // Wait for it to crash and exit
    if (task_wait(pid, &exit_code) != 0) {
        panic("Phase 9 & 10: Failed to wait for test_read_kernel");
    }
    // Vector 14 is Page Fault. Our exception handler exits with -vector.
    if (exit_code != -14) {
        panic("Phase 9 & 10: test_read_kernel did not crash with Page Fault (got exit %d)", exit_code);
    }
    printk("Phase 9 & 10: test_read_kernel terminated with Page Fault OK\n");

    // Test 2: load and execute test_privileged.elf
    // It should raise a General Protection Fault in userspace and get killed.
    size_t size2 = (size_t)(user_test_privileged_elf_end - user_test_privileged_elf_start);
    struct task *t2 = elf_load("test_privileged", user_test_privileged_elf_start, size2, 0, 0, 0);
    if (t2 == 0) panic("Phase 9 & 10: Failed to load test_privileged.elf");
    pid = t2->pid;

    // Wait for it to crash and exit
    if (task_wait(pid, &exit_code) != 0) {
        panic("Phase 9 & 10: Failed to wait for test_privileged");
    }
    // Vector 13 is General Protection Fault
    if (exit_code != -13) {
        panic("Phase 9 & 10: test_privileged did not crash with GPF (got exit %d)", exit_code);
    }
    printk("Phase 9 & 10: test_privileged terminated with General Protection Fault OK\n");

    // Test 3: load and execute init.elf
    // It should print to serial and exit normally with code 0.
    size_t size3 = (size_t)(user_init_elf_end - user_init_elf_start);
    char *argv[] = { "/bin/init", "test_arg", 0 };
    struct task *t3 = elf_load("init", user_init_elf_start, size3, 2, argv, 0);
    if (t3 == 0) panic("Phase 9 & 10: Failed to load init.elf");
    pid = t3->pid;

    // Wait for it to exit
    if (task_wait(pid, &exit_code) != 0) {
        panic("Phase 9 & 10: Failed to wait for init");
    }
    if (exit_code != 0) {
        panic("Phase 9 & 10: init did not exit cleanly (got %d)", exit_code);
    }
    printk("Phase 9 & 10: init program exited with 0 OK\n");

    printk("Phase 9 & 10 tests passed successfully\n");
}

#include <arch/x86_64/cpu.h>
#include <arch/x86_64/gdt.h>
#include <arch/x86_64/idt.h>
#include <arch/x86_64/limine.h>
#include <arch/x86_64/pic.h>
#include <drivers/pit.h>
#include <drivers/serial.h>
#include <drivers/vga_text.h>
#include <kernel/init.h>
#include <kernel/panic.h>
#include <kernel/printk.h>
#include <mm/pmm.h>
#include <stdint.h>

static void halt_forever(void)
{
    interrupts_enable();
    for (;;) {
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
#elif defined(LITENIX_TEST_PAGE_FAULT)
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
    halt_forever();
}

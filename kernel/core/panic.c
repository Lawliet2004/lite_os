#include <kernel/panic.h>
#include <kernel/printk.h>
#include <arch/x86_64/smp.h>
#include <stdarg.h>

void (*volatile panic_hook)(void) = 0;

NORETURN void panic_at(const char *file, int line, const char *fmt, ...)
{
    if (panic_hook) {
        panic_hook();
    }

    /* Phase 32 followup: tell the other CPUs to halt before we
     * print the panic. The IPI handler on the receiving side
     * enters its own cli/hlt loop. The BSP still prints the
     * panic here (the order doesn't matter — the broadcast is
     * fast and the other CPUs will halt before they can do
     * anything else). */
    smp_broadcast_panic();

    printk("\nKERNEL PANIC\n");
    printk("Location: %s:%d\n", file, line);
    printk("Message: ");

    va_list args;
    va_start(args, fmt);
    vprintk(fmt, args);
    va_end(args);

    printk("\nSystem halted.\n");

    for (;;) {
        __asm__ volatile ("cli; hlt");
    }
}

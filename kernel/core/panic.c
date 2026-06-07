#include <kernel/panic.h>
#include <kernel/printk.h>
#include <stdarg.h>

NORETURN void panic_at(const char *file, int line, const char *fmt, ...)
{
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

#ifndef LITENIX_KERNEL_PRINTK_H
#define LITENIX_KERNEL_PRINTK_H

#include <stdarg.h>

void printk(const char *fmt, ...);
void vprintk(const char *fmt, va_list args);

#endif

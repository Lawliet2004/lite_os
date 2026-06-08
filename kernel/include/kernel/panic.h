#ifndef LITENIX_KERNEL_PANIC_H
#define LITENIX_KERNEL_PANIC_H

#include <kernel/compiler.h>

NORETURN void panic_at(const char *file, int line, const char *fmt, ...);

extern void (*panic_hook)(void);

#define panic(...) panic_at(__FILE__, __LINE__, __VA_ARGS__)

#define KASSERT(expr) \
    do { \
        if (!(expr)) { \
            panic("assertion failed: %s", #expr); \
        } \
    } while (0)

#endif

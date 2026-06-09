#include <stddef.h>

/* Minimal fake ELF interpreter: prints evidence and exits cleanly.
 * This is loaded by the kernel when a PT_INTERP binary runs.
 * It proves the kernel's dynamic loader path works without needing
 * a full libc dynamic linker.
 */

static inline void syscall3(long n, long a, long b, long c)
{
    __asm__ volatile("syscall" : : "a"(n), "D"(a), "S"(b), "d"(c) : "rcx","r11","memory");
}

static inline long syscall1(long n, long a)
{
    long r;
    __asm__ volatile("syscall" : "=a"(r) : "a"(n), "D"(a) : "rcx","r11","memory");
    return r;
}

static size_t slen(const char *s)
{
    size_t n = 0;
    while (s[n]) n++;
    return n;
}

void _start(void)
{
    const char msg[] = "Dynamic kernel loader path works!\n";
    syscall3(1, 1, (long)msg, (long)slen(msg)); /* write(1, msg, len) */
    syscall1(60, 0); /* exit(0) */
    __builtin_unreachable();
}

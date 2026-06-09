/* Dynamic binary test: compiled with a custom PT_INTERP pointing to fake_interp.
 * The binary's .text is mostly empty since fake_interp prints the evidence.
 */
#include <stddef.h>

void _start(void)
{
    __asm__ volatile("mov $60, %%rax; xor %%rdi, %%rdi; syscall" ::: "memory");
    __builtin_unreachable();
}

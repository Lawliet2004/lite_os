#ifndef LITENIX_ARCH_X86_64_CPU_H
#define LITENIX_ARCH_X86_64_CPU_H

static inline void interrupts_enable(void)
{
    __asm__ volatile ("sti");
}

static inline void interrupts_disable(void)
{
    __asm__ volatile ("cli");
}

static inline void cpu_halt(void)
{
    __asm__ volatile ("hlt");
}

#endif

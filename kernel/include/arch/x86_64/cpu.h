#ifndef LITENIX_ARCH_X86_64_CPU_H
#define LITENIX_ARCH_X86_64_CPU_H

#include <stdbool.h>
#include <stdint.h>

extern bool cpu_smap_supported;

static inline void interrupts_enable(void)
{
    __asm__ volatile ("sti");
}

static inline void interrupts_disable(void)
{
    __asm__ volatile ("cli");
}

static inline bool save_interrupts_and_disable(void)
{
    uint64_t rflags;
    __asm__ volatile (
        "pushfq\n"
        "pop %0\n"
        : "=r"(rflags)
        :
        : "memory");
    bool was_enabled = (rflags & (1ULL << 9)) != 0;
    interrupts_disable();
    return was_enabled;
}

static inline void restore_interrupts(bool was_enabled)
{
    if (was_enabled) {
        interrupts_enable();
    } else {
        interrupts_disable();
    }
}

static inline void cpu_halt(void)
{
    __asm__ volatile ("hlt");
}

static inline void stac(void)
{
    if (cpu_smap_supported) {
        __asm__ volatile ("stac" ::: "cc");
    }
}

static inline void clac(void)
{
    if (cpu_smap_supported) {
        __asm__ volatile ("clac" ::: "cc");
    }
}

static inline void write_fs_base(uint64_t val)
{
    uint32_t lo = val & 0xFFFFFFFF;
    uint32_t hi = val >> 32;
    __asm__ volatile ("wrmsr" : : "c"(0xC0000100), "a"(lo), "d"(hi) : "memory");
}

#endif

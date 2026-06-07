#include <arch/x86_64/gdt.h>
#include <kernel/compiler.h>
#include <stdint.h>

#define TSS_AVAILABLE_64 0x89

struct gdtr {
    uint16_t limit;
    uint64_t base;
} PACKED;

struct tss64 {
    uint32_t reserved0;
    uint64_t rsp0;
    uint64_t rsp1;
    uint64_t rsp2;
    uint64_t reserved1;
    uint64_t ist1;
    uint64_t ist2;
    uint64_t ist3;
    uint64_t ist4;
    uint64_t ist5;
    uint64_t ist6;
    uint64_t ist7;
    uint64_t reserved2;
    uint16_t reserved3;
    uint16_t iomap_base;
} PACKED;

static uint64_t gdt[5];
static struct tss64 tss;
static uint8_t kernel_rsp0_stack[16384] __attribute__((aligned(16)));
static uint8_t double_fault_stack[16384] __attribute__((aligned(16)));

extern void gdt_load(const struct gdtr *gdtr, uint16_t code_selector,
    uint16_t data_selector, uint16_t tss_selector);

uint64_t read_rsp(void)
{
    uint64_t value;
    __asm__ volatile ("mov %%rsp, %0" : "=r"(value));
    return value;
}

uint64_t read_rbp(void)
{
    uint64_t value;
    __asm__ volatile ("mov %%rbp, %0" : "=r"(value));
    return value;
}

static void set_tss_descriptor(uint64_t base, uint32_t limit)
{
    gdt[3] =
        ((uint64_t)(limit & 0xFFFF)) |
        ((base & 0xFFFFFFULL) << 16) |
        ((uint64_t)TSS_AVAILABLE_64 << 40) |
        (((uint64_t)(limit >> 16) & 0xFULL) << 48) |
        (((base >> 24) & 0xFFULL) << 56);

    gdt[4] = base >> 32;
}

void gdt_init(void)
{
    tss.rsp0 = (uint64_t)(kernel_rsp0_stack + sizeof(kernel_rsp0_stack));
    tss.ist1 = (uint64_t)(double_fault_stack + sizeof(double_fault_stack));
    tss.iomap_base = sizeof(tss);

    gdt[0] = 0;
    gdt[1] = 0x00AF9A000000FFFFULL;
    gdt[2] = 0x00AF92000000FFFFULL;
    set_tss_descriptor((uint64_t)&tss, sizeof(tss) - 1);

    const struct gdtr gdtr = {
        .limit = sizeof(gdt) - 1,
        .base = (uint64_t)&gdt,
    };

    gdt_load(&gdtr, KERNEL_CODE_SELECTOR, KERNEL_DATA_SELECTOR, TSS_SELECTOR);
}

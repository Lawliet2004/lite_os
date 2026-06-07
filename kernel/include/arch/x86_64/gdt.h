#ifndef LITENIX_ARCH_X86_64_GDT_H
#define LITENIX_ARCH_X86_64_GDT_H

#include <stdint.h>

#define KERNEL_CODE_SELECTOR 0x08
#define KERNEL_DATA_SELECTOR 0x10
#define TSS_SELECTOR 0x18

void gdt_init(void);
uint64_t read_rsp(void);
uint64_t read_rbp(void);

#endif

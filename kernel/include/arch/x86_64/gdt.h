#ifndef LITENIX_ARCH_X86_64_GDT_H
#define LITENIX_ARCH_X86_64_GDT_H

#include <stdint.h>

#define KERNEL_CODE_SELECTOR 0x08
#define KERNEL_DATA_SELECTOR 0x10
#define TSS_SELECTOR         0x18
/* TSS occupies two GDT slots (0x18 and 0x20) on x86_64 */
#define USER_DATA_SELECTOR   0x28   /* GDT[5], DPL=3 */
#define USER_CODE_SELECTOR   0x30   /* GDT[6], DPL=3 */
/* RPL=3 variants used for segment registers in user mode */
#define USER_DATA_RPL3       (USER_DATA_SELECTOR | 3)
#define USER_CODE_RPL3       (USER_CODE_SELECTOR | 3)

void gdt_init(void);
void tss_set_rsp0(uint64_t rsp0);
uint64_t read_rsp(void);
uint64_t read_rbp(void);

#endif

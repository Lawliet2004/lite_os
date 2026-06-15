#ifndef LITENIX_ARCH_X86_64_GDT_H
#define LITENIX_ARCH_X86_64_GDT_H

#include <stdint.h>

#define KERNEL_CODE_SELECTOR 0x08
#define KERNEL_DATA_SELECTOR 0x10
#define TSS_SELECTOR         0x28   /* GDT[5] -- CPU 0's TSS in Phase 3b */
/* TSS occupies two GDT slots per CPU. CPU N's TSS is at selector
 * TSS_SELECTOR + N*16 (0x28, 0x38, 0x48, ...). */
#define USER_DATA_SELECTOR   0x18   /* GDT[3], DPL=3 */
#define USER_CODE_SELECTOR   0x20   /* GDT[4], DPL=3 */
/* RPL=3 variants used for segment registers in user mode */
#define USER_DATA_RPL3       (USER_DATA_SELECTOR | 3)
#define USER_CODE_RPL3       (USER_CODE_SELECTOR | 3)

void gdt_init(void);
void gdt_init_cpu_tss(int cpu_id, uint64_t kernel_stack_top);
uint16_t gdt_tss_selector(int cpu_id);
void tss_set_rsp0(uint64_t rsp0);
uint64_t read_rsp(void);
uint64_t read_rbp(void);

#endif

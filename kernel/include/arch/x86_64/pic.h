#ifndef LITENIX_ARCH_X86_64_PIC_H
#define LITENIX_ARCH_X86_64_PIC_H

#include <stdint.h>

#define PIC_REMAP_OFFSET 32
#define PIC_IRQ_COUNT 16

void pic_remap(void);
void pic_mask_all(void);
void pic_unmask_irq(uint8_t irq);
void pic_send_eoi(uint8_t irq);

#endif

#ifndef LITENIX_DRIVERS_PIT_H
#define LITENIX_DRIVERS_PIT_H

#include <stdint.h>

#define PIT_DEFAULT_HZ 100

void pit_init(uint32_t frequency_hz);
void pit_on_tick(void);
uint64_t pit_ticks(void);

#endif

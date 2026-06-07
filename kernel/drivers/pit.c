#include <arch/x86_64/io.h>
#include <drivers/pit.h>
#include <stdint.h>

#define PIT_INPUT_CLOCK_HZ 1193182U
#define PIT_CHANNEL0 0x40
#define PIT_COMMAND 0x43
#define PIT_MODE_RATE_GENERATOR 0x36

static volatile uint64_t ticks;

void pit_init(uint32_t frequency_hz)
{
    if (frequency_hz == 0) {
        frequency_hz = PIT_DEFAULT_HZ;
    }

    uint32_t divisor = PIT_INPUT_CLOCK_HZ / frequency_hz;
    if (divisor == 0) {
        divisor = 1;
    }
    if (divisor > 0xFFFF) {
        divisor = 0xFFFF;
    }

    outb(PIT_COMMAND, PIT_MODE_RATE_GENERATOR);
    outb(PIT_CHANNEL0, (uint8_t)(divisor & 0xFF));
    outb(PIT_CHANNEL0, (uint8_t)((divisor >> 8) & 0xFF));
}

void pit_on_tick(void)
{
    ticks++;
}

uint64_t pit_ticks(void)
{
    return ticks;
}

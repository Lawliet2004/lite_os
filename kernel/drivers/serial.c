#include <arch/x86_64/io.h>
#include <drivers/serial.h>

#define COM1_PORT 0x3F8
#define SERIAL_DATA_PORT(base) ((base) + 0)
#define SERIAL_INTERRUPT_ENABLE_PORT(base) ((base) + 1)
#define SERIAL_FIFO_CONTROL_PORT(base) ((base) + 2)
#define SERIAL_LINE_CONTROL_PORT(base) ((base) + 3)
#define SERIAL_MODEM_CONTROL_PORT(base) ((base) + 4)
#define SERIAL_LINE_STATUS_PORT(base) ((base) + 5)

#define SERIAL_LINE_ENABLE_DLAB 0x80
#define SERIAL_LINE_8N1 0x03
#define SERIAL_FIFO_ENABLE_CLEAR 0xC7
#define SERIAL_MODEM_DEFAULT 0x0B
#define SERIAL_TRANSMIT_EMPTY 0x20

static bool initialized;

void serial_init(void)
{
    outb(SERIAL_INTERRUPT_ENABLE_PORT(COM1_PORT), 0x00);
    outb(SERIAL_LINE_CONTROL_PORT(COM1_PORT), SERIAL_LINE_ENABLE_DLAB);
    outb(SERIAL_DATA_PORT(COM1_PORT), 0x03);
    outb(SERIAL_INTERRUPT_ENABLE_PORT(COM1_PORT), 0x00);
    outb(SERIAL_LINE_CONTROL_PORT(COM1_PORT), SERIAL_LINE_8N1);
    outb(SERIAL_FIFO_CONTROL_PORT(COM1_PORT), SERIAL_FIFO_ENABLE_CLEAR);
    outb(SERIAL_MODEM_CONTROL_PORT(COM1_PORT), SERIAL_MODEM_DEFAULT);

    initialized = true;
}

bool serial_is_initialized(void)
{
    return initialized;
}

void serial_write_char(char ch)
{
    if (!initialized) {
        return;
    }

    if (ch == '\n') {
        serial_write_char('\r');
    }

    while ((inb(SERIAL_LINE_STATUS_PORT(COM1_PORT)) & SERIAL_TRANSMIT_EMPTY) == 0) {
    }

    outb(SERIAL_DATA_PORT(COM1_PORT), (uint8_t)ch);
}

void serial_write_string(const char *text)
{
    while (*text != '\0') {
        serial_write_char(*text++);
    }
}

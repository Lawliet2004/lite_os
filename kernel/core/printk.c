#include <drivers/serial.h>
#include <drivers/vga_text.h>
#include <kernel/printk.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

extern void kmsg_put_char(char ch);
static void put_char(char ch)
{
    serial_write_char(ch);
    vga_text_write_char(ch);
    kmsg_put_char(ch);
}

static void put_string(const char *text)
{
    if (text == 0) {
        text = "(null)";
    }

    while (*text != '\0') {
        put_char(*text++);
    }
}

static void put_unsigned(uint64_t value, unsigned base, bool uppercase)
{
    char buffer[32];
    const char *digits = uppercase
        ? "0123456789ABCDEF"
        : "0123456789abcdef";
    size_t index = 0;

    if (value == 0) {
        put_char('0');
        return;
    }

    while (value != 0 && index < sizeof(buffer)) {
        buffer[index++] = digits[value % base];
        value /= base;
    }

    while (index > 0) {
        put_char(buffer[--index]);
    }
}

static void put_signed(int64_t value)
{
    if (value < 0) {
        put_char('-');
        put_unsigned((uint64_t)(-value), 10, false);
        return;
    }

    put_unsigned((uint64_t)value, 10, false);
}

void vprintk(const char *fmt, va_list args)
{
    while (*fmt != '\0') {
        if (*fmt != '%') {
            put_char(*fmt++);
            continue;
        }

        fmt++;

        bool long_long = false;
        if (*fmt == 'l') {
            fmt++;
            if (*fmt == 'l') {
                long_long = true;
                fmt++;
            }
        }

        switch (*fmt) {
        case '%':
            put_char('%');
            break;
        case 'c':
            put_char((char)va_arg(args, int));
            break;
        case 's':
            put_string(va_arg(args, const char *));
            break;
        case 'd':
        case 'i':
            if (long_long) {
                put_signed(va_arg(args, long long));
            } else {
                put_signed(va_arg(args, int));
            }
            break;
        case 'u':
            if (long_long) {
                put_unsigned(va_arg(args, unsigned long long), 10, false);
            } else {
                put_unsigned(va_arg(args, unsigned int), 10, false);
            }
            break;
        case 'x':
            if (long_long) {
                put_unsigned(va_arg(args, unsigned long long), 16, false);
            } else {
                put_unsigned(va_arg(args, unsigned int), 16, false);
            }
            break;
        case 'X':
            if (long_long) {
                put_unsigned(va_arg(args, unsigned long long), 16, true);
            } else {
                put_unsigned(va_arg(args, unsigned int), 16, true);
            }
            break;
        case 'p':
            put_string("0x");
            put_unsigned((uintptr_t)va_arg(args, void *), 16, false);
            break;
        default:
            put_char('%');
            put_char(*fmt);
            break;
        }

        if (*fmt != '\0') {
            fmt++;
        }
    }
}

void printk(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    vprintk(fmt, args);
    va_end(args);
}

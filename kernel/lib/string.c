#include <lib/string.h>
#include <stdint.h>

void *memset(void *dest, int value, size_t count)
{
    uint8_t *out = dest;
    for (size_t i = 0; i < count; i++) {
        out[i] = (uint8_t)value;
    }
    return dest;
}

void *memcpy(void *dest, const void *src, size_t count)
{
    uint8_t *out = dest;
    const uint8_t *in = src;
    for (size_t i = 0; i < count; i++) {
        out[i] = in[i];
    }
    return dest;
}

size_t strlen(const char *text)
{
    size_t length = 0;
    while (text[length] != '\0') {
        length++;
    }
    return length;
}

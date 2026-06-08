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

int memcmp(const void *lhs, const void *rhs, size_t count)
{
    const uint8_t *a = lhs;
    const uint8_t *b = rhs;
    for (size_t i = 0; i < count; i++) {
        if (a[i] != b[i]) {
            return (int)(a[i] - b[i]);
        }
    }
    return 0;
}

int strcmp(const char *s1, const char *s2)
{
    while (*s1 && (*s1 == *s2)) {
        s1++;
        s2++;
    }
    return *(const unsigned char *)s1 - *(const unsigned char *)s2;
}

char *strcpy(char *dest, const char *src)
{
    char *d = dest;
    while ((*d++ = *src++)) {
    }
    return dest;
}

char *strcat(char *dest, const char *src)
{
    char *d = dest;
    while (*d) {
        d++;
    }
    while ((*d++ = *src++)) {
    }
    return dest;
}

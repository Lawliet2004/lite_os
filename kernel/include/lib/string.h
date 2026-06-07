#ifndef LITENIX_LIB_STRING_H
#define LITENIX_LIB_STRING_H

#include <stddef.h>

void *memset(void *dest, int value, size_t count);
void *memcpy(void *dest, const void *src, size_t count);
size_t strlen(const char *text);

#endif

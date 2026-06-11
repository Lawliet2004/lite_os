#ifndef LITENIX_LIB_STRING_H
#define LITENIX_LIB_STRING_H

#include <stddef.h>

void *memset(void *dest, int value, size_t count);
void *memcpy(void *dest, const void *src, size_t count);
int memcmp(const void *lhs, const void *rhs, size_t count);
size_t strlen(const char *text);
int strcmp(const char *s1, const char *s2);
char *strcpy(char *dest, const char *src);
char *strcat(char *dest, const char *src);
int strncmp(const char *s1, const char *s2, size_t n);

#endif

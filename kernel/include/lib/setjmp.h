#ifndef _LIB_SETJMP_H
#define _LIB_SETJMP_H

#include <stdint.h>

typedef uint64_t jmp_buf_t[8];

int ltenix_setjmp(jmp_buf_t env);
void ltenix_longjmp(jmp_buf_t env, int val) __attribute__((noreturn));

#endif /* _LIB_SETJMP_H */

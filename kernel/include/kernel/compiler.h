#ifndef LITENIX_KERNEL_COMPILER_H
#define LITENIX_KERNEL_COMPILER_H

#define NORETURN __attribute__((noreturn))
#define PACKED __attribute__((packed))
#define USED __attribute__((used))
#define SECTION(name) __attribute__((section(name)))

#endif

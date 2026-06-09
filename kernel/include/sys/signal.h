#ifndef LITENIX_SYS_SIGNAL_H
#define LITENIX_SYS_SIGNAL_H

#include <stdint.h>
#include <stddef.h>

struct sigaction_linux {
    void (*sa_handler)(int);
    uint64_t sa_flags;
    void (*sa_restorer)(void);
    uint64_t sa_mask;
};

struct sigcontext_linux {
    uint64_t r8;
    uint64_t r9;
    uint64_t r10;
    uint64_t r11;
    uint64_t r12;
    uint64_t r13;
    uint64_t r14;
    uint64_t r15;
    uint64_t rdi;
    uint64_t rsi;
    uint64_t rbp;
    uint64_t rbx;
    uint64_t rdx;
    uint64_t rax;
    uint64_t rcx;
    uint64_t rsp;
    uint64_t rip;
    uint64_t eflags;
    uint16_t cs;
    uint16_t gs;
    uint16_t fs;
    uint16_t ss;
    uint64_t err;
    uint64_t trapno;
    uint64_t oldmask;
    uint64_t cr2;
};

struct ucontext_linux {
    uint64_t uc_flags;
    void *uc_link;
    struct {
        void *ss_sp;
        int ss_flags;
        size_t ss_size;
    } uc_stack;
    struct sigcontext_linux uc_mcontext;
    uint64_t uc_sigmask;
};

struct siginfo_linux {
    int si_signo;
    int si_errno;
    int si_code;
};

struct rt_sigframe {
    void (*pretend_retaddr)(void);
    struct siginfo_linux info;
    struct ucontext_linux uc;
};

#endif

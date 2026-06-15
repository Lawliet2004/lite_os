#include "libc_lite.h"
#include <stdbool.h>

__asm__(
".global _start\n"
"_start:\n"
"    movq (%rsp), %rdi\n"
"    leaq 8(%rsp), %rsi\n"
"    leaq 16(%rsp,%rdi,8), %rdx\n"
"    andq $-16, %rsp\n"
"    call main\n"
"    movq %rax, %rdi\n"
"    call exit\n"
"\n"
".global clone_thread\n"
"clone_thread:\n"
"    # rdi = fn, rsi = child_stack, rdx = arg\n"
"    subq $16, %rsi\n"
"    movq %rdx, 8(%rsi)\n"
"    movq %rdi, 0(%rsi)\n"
"    \n"
"    # sys_clone(flags, child_stack, parent_tid, child_tid, newtls)\n"
"    movq $0x00010f00, %rdi  # CLONE_VM|CLONE_FS|CLONE_FILES|CLONE_SIGHAND|CLONE_THREAD\n"
"    # rsi is already set to the new child_stack pointer\n"
"    movq $0, %rdx\n"
"    movq $0, %r10\n"
"    movq $0, %r8\n"
"    movq $56, %rax          # SYS_clone\n"
"    syscall\n"
"    \n"
"    testq %rax, %rax\n"
"    jz child_start\n"
"    ret                     # parent returns TID\n"
"\n"
"child_start:\n"
"    popq %rax               # pop fn\n"
"    popq %rdi               # pop arg\n"
"    call *%rax              # call fn(arg)\n"
"    movq %rax, %rdi\n"
"    call exit               # exit(status)\n"
);

static inline int64_t syscall0(int64_t num)
{
    int64_t ret;
    __asm__ volatile("syscall" : "=a"(ret) : "a"(num) : "rcx", "r11", "memory");
    return ret;
}

static inline int64_t syscall1(int64_t num, int64_t arg1)
{
    int64_t ret;
    __asm__ volatile("syscall" : "=a"(ret) : "a"(num), "D"(arg1) : "rcx", "r11", "memory");
    return ret;
}

static inline int64_t syscall2(int64_t num, int64_t arg1, int64_t arg2)
{
    int64_t ret;
    __asm__ volatile("syscall" : "=a"(ret) : "a"(num), "D"(arg1), "S"(arg2) : "rcx", "r11", "memory");
    return ret;
}

static inline int64_t syscall3(int64_t num, int64_t arg1, int64_t arg2, int64_t arg3)
{
    int64_t ret;
    __asm__ volatile("syscall" : "=a"(ret) : "a"(num), "D"(arg1), "S"(arg2), "d"(arg3) : "rcx", "r11", "memory");
    return ret;
}

static inline int64_t syscall4(int64_t num, int64_t arg1, int64_t arg2, int64_t arg3, int64_t arg4)
{
    int64_t ret;
    register int64_t r10 __asm__("r10") = arg4;
    __asm__ volatile("syscall" : "=a"(ret) : "a"(num), "D"(arg1), "S"(arg2), "d"(arg3), "r"(r10) : "rcx", "r11", "memory");
    return ret;
}

static inline int64_t syscall5(int64_t num, int64_t arg1, int64_t arg2, int64_t arg3, int64_t arg4, int64_t arg5)
{
    int64_t ret;
    register int64_t r10 __asm__("r10") = arg4;
    register int64_t r8  __asm__("r8")  = arg5;
    __asm__ volatile("syscall" : "=a"(ret) : "a"(num), "D"(arg1), "S"(arg2), "d"(arg3), "r"(r10), "r"(r8) : "rcx", "r11", "memory");
    return ret;
}

static inline int64_t __attribute__((unused))
syscall6(int64_t num, int64_t a1, int64_t a2, int64_t a3,
         int64_t a4, int64_t a5, int64_t a6)
{
    int64_t ret;
    register int64_t r10 __asm__("r10") = a4;
    register int64_t r8  __asm__("r8")  = a5;
    register int64_t r9  __asm__("r9")  = a6;
    __asm__ volatile("syscall" : "=a"(ret) : "a"(num), "D"(a1), "S"(a2), "d"(a3), "r"(r10), "r"(r8), "r"(r9) : "rcx", "r11", "memory");
    return ret;
}

/* ------------------------------------------------------------------ */
/* File I/O                                                             */
/* ------------------------------------------------------------------ */

int open(const char *pathname, int flags)
{
    return (int)syscall2(2, (int64_t)pathname, (int64_t)flags);
}

int openat(int dirfd, const char *pathname, int flags, int mode)
{
    return (int)syscall4(257, (int64_t)dirfd, (int64_t)pathname, (int64_t)flags, (int64_t)mode);
}

int close(int fd)
{
    return (int)syscall1(3, (int64_t)fd);
}

ssize_t read(int fd, void *buf, size_t count)
{
    return (ssize_t)syscall3(0, (int64_t)fd, (int64_t)buf, (int64_t)count);
}

ssize_t write(int fd, const void *buf, size_t count)
{
    return (ssize_t)syscall3(1, (int64_t)fd, (int64_t)buf, (int64_t)count);
}

off_t lseek(int fd, off_t offset, int whence)
{
    return (off_t)syscall3(8, (int64_t)fd, (int64_t)offset, (int64_t)whence);
}

int fstat(int fd, struct stat *statbuf)
{
    return (int)syscall2(5, (int64_t)fd, (int64_t)statbuf);
}

int stat(const char *pathname, struct stat *statbuf)
{
    return (int)syscall2(4, (int64_t)pathname, (int64_t)statbuf);
}

int fstatat(int dirfd, const char *pathname, struct stat *statbuf, int flags)
{
    return (int)syscall4(262, (int64_t)dirfd, (int64_t)pathname, (int64_t)statbuf, (int64_t)flags);
}

int statx(int dirfd, const char *pathname, int flags, unsigned int mask, struct statx *statxbuf)
{
    return (int)syscall6(332, (int64_t)dirfd, (int64_t)pathname, (int64_t)flags, (int64_t)mask, (int64_t)statxbuf, 0);
}

int access(const char *pathname, int mode)
{
    return (int)syscall2(21, (int64_t)pathname, (int64_t)mode);
}

int faccessat(int dirfd, const char *pathname, int mode, int flags)
{
    return (int)syscall4(269, (int64_t)dirfd, (int64_t)pathname, (int64_t)mode, (int64_t)flags);
}

ssize_t readlink(const char *pathname, char *buf, size_t bufsiz)
{
    return (ssize_t)syscall3(89, (int64_t)pathname, (int64_t)buf, (int64_t)bufsiz);
}

ssize_t readlinkat(int dirfd, const char *pathname, char *buf, size_t bufsiz)
{
    return (ssize_t)syscall4(267, (int64_t)dirfd, (int64_t)pathname, (int64_t)buf, (int64_t)bufsiz);
}

int getdents64(unsigned int fd, struct linux_dirent64 *dirp, unsigned int count)
{
    return (int)syscall3(217, (int64_t)fd, (int64_t)dirp, (int64_t)count);
}

int ioctl(int fd, unsigned long request, void *argp)
{
    return (int)syscall3(16, (int64_t)fd, (int64_t)request, (int64_t)argp);
}

int dup(int fd)
{
    return (int)syscall1(32, (int64_t)fd);
}

int dup2(int oldfd, int newfd)
{
    return (int)syscall2(33, (int64_t)oldfd, (int64_t)newfd);
}

/* ------------------------------------------------------------------ */
/* Directory / FS                                                       */
/* ------------------------------------------------------------------ */

int mkdir(const char *pathname, int mode)
{
    return (int)syscall2(83, (int64_t)pathname, (int64_t)mode);
}

int rmdir(const char *pathname)
{
    return (int)syscall1(84, (int64_t)pathname);
}

int unlink(const char *pathname)
{
    return (int)syscall1(87, (int64_t)pathname);
}

int rename(const char *oldpath, const char *newpath)
{
    return (int)syscall2(82, (int64_t)oldpath, (int64_t)newpath);
}

int chdir(const char *path)
{
    return (int)syscall1(80, (int64_t)path);
}

char *getcwd(char *buf, size_t size)
{
    int64_t ret = syscall2(79, (int64_t)buf, (int64_t)size);
    return (ret < 0) ? 0 : buf;
}

int symlink(const char *target, const char *linkpath)
{
    return (int)syscall2(88, (int64_t)target, (int64_t)linkpath);
}

int chmod_libc(const char *pathname, uint32_t mode)
{
    return (int)syscall2(90, (int64_t)pathname, (int64_t)mode);
}

int fchmod_libc(int fd, uint32_t mode)
{
    return (int)syscall2(91, (int64_t)fd, (int64_t)mode);
}

int chown_libc(const char *pathname, int32_t owner, int32_t group)
{
    return (int)syscall3(92, (int64_t)pathname, (int64_t)owner, (int64_t)group);
}

int fchown_libc(int fd, int32_t owner, int32_t group)
{
    return (int)syscall3(93, (int64_t)fd, (int64_t)owner, (int64_t)group);
}

uint32_t umask_libc(uint32_t mask)
{
    return (uint32_t)syscall1(95, (int64_t)mask);
}

/* ------------------------------------------------------------------ */
/* Process                                                              */
/* ------------------------------------------------------------------ */

int execve(const char *pathname, char *const argv[], char *const envp[])
{
    return (int)syscall3(59, (int64_t)pathname, (int64_t)argv, (int64_t)envp);
}

void exit(int status)
{
    syscall1(60, (int64_t)status);
    for (;;) __asm__ volatile("hlt");
}

int fork(void)
{
    return (int)syscall1(57, 0);
}

int wait4(int pid, int *wstatus, int options, void *rusage)
{
    return (int)syscall4(61, (int64_t)pid, (int64_t)wstatus, (int64_t)options, (int64_t)rusage);
}

int getpid(void)
{
    return (int)syscall0(39);
}

int gettid(void)
{
    return (int)syscall0(186);
}

int getppid(void)
{
    return (int)syscall0(110);
}

int setpgid(int pid, int pgid)
{
    return (int)syscall2(109, (int64_t)pid, (int64_t)pgid);
}

int getpgid(int pid)
{
    return (int)syscall1(121, (int64_t)pid);
}

int setsid(void)
{
    return (int)syscall0(112);
}

int getsid(int pid)
{
    return (int)syscall1(124, (int64_t)pid);
}

__asm__(
"    .global __restore_rt\n"
"__restore_rt:\n"
"    movq $15, %rax\n"
"    syscall\n"
);

void __restore_rt(void);

int sigaction(int signum, const struct sigaction *act, struct sigaction *oldact)
{
    struct sigaction kact;
    if (act) {
        kact = *act;
        kact.sa_flags |= 0x04000000; // SA_RESTORER
        kact.sa_restorer = __restore_rt;
        act = &kact;
    }
    return (int)syscall4(13, (int64_t)signum, (int64_t)act, (int64_t)oldact, 8);
}

int sigprocmask(int how, const uint64_t *set, uint64_t *oldset)
{
    return (int)syscall4(14, (int64_t)how, (int64_t)set, (int64_t)oldset, 8);
}

int kill(int pid, int sig)
{
    return (int)syscall2(62, (int64_t)pid, (int64_t)sig);
}

int clone(unsigned long flags, void *child_stack, int *parent_tid, int *child_tid, unsigned long newtls)
{
    return (int)syscall6(56, (int64_t)flags, (int64_t)child_stack, (int64_t)parent_tid, (int64_t)child_tid, (int64_t)newtls, 0);
}

int futex(uint32_t *uaddr, int op, uint32_t val, const void *timeout, uint32_t *uaddr2, uint32_t val3)
{
    return (int)syscall6(202, (int64_t)uaddr, (int64_t)op, (int64_t)val, (int64_t)timeout, (int64_t)uaddr2, (int64_t)val3);
}

int arch_prctl(int code, unsigned long addr)
{
    return (int)syscall2(158, (int64_t)code, (int64_t)addr);
}

int set_tid_address(int *tidptr)
{
    return (int)syscall1(218, (int64_t)tidptr);
}

int set_robust_list(void *head, size_t len)
{
    return (int)syscall2(273, (int64_t)head, (int64_t)len);
}

int get_robust_list(int pid, void **head, size_t *len)
{
    return (int)syscall3(274, (int64_t)pid, (int64_t)head, (int64_t)len);
}

int uname(struct utsname *buf)
{
    return (int)syscall1(63, (int64_t)buf);
}

ssize_t getrandom(void *buf, size_t buflen, unsigned int flags)
{
    return (ssize_t)syscall3(318, (int64_t)buf, (int64_t)buflen, (int64_t)flags);
}

int getrlimit(int resource, struct rlimit *rlim)
{
    return (int)syscall2(97, (int64_t)resource, (int64_t)rlim);
}

int prlimit64(int pid, int resource, const struct rlimit *new_limit, struct rlimit *old_limit)
{
    return (int)syscall4(302, (int64_t)pid, (int64_t)resource, (int64_t)new_limit, (int64_t)old_limit);
}

int getresuid(uint32_t *ruid, uint32_t *euid, uint32_t *suid)
{
    return (int)syscall3(118, (int64_t)ruid, (int64_t)euid, (int64_t)suid);
}

int setresuid(uint32_t ruid, uint32_t euid, uint32_t suid)
{
    return (int)syscall3(117, (int64_t)ruid, (int64_t)euid, (int64_t)suid);
}

int getresgid(uint32_t *rgid, uint32_t *egid, uint32_t *sgid)
{
    return (int)syscall3(120, (int64_t)rgid, (int64_t)egid, (int64_t)sgid);
}

int setresgid(uint32_t rgid, uint32_t egid, uint32_t sgid)
{
    return (int)syscall3(119, (int64_t)rgid, (int64_t)egid, (int64_t)sgid);
}

int prctl(int option, unsigned long arg2, unsigned long arg3, unsigned long arg4, unsigned long arg5)
{
    return (int)syscall5(157, (int64_t)option, (int64_t)arg2, (int64_t)arg3, (int64_t)arg4, (int64_t)arg5);
}


/* ------------------------------------------------------------------ */
/* Memory                                                               */
/* ------------------------------------------------------------------ */

void *brk_syscall(unsigned long addr)
{
    int64_t ret = syscall1(12, (int64_t)addr);
    return (void *)ret;
}

void *mmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset)
{
    int64_t ret = syscall6(9, (int64_t)addr, (int64_t)length, (int64_t)prot, (int64_t)flags, (int64_t)fd, (int64_t)offset);
    return (void *)ret;
}

int munmap(void *addr, size_t length)
{
    return (int)syscall2(11, (int64_t)addr, (int64_t)length);
}

void *mremap(void *old_address, size_t old_size, size_t new_size, int flags, void *new_address)
{
    int64_t ret = syscall5(25, (int64_t)old_address, (int64_t)old_size, (int64_t)new_size, (int64_t)flags, (int64_t)new_address);
    return (void *)ret;
}

int madvise(void *addr, size_t length, int advice)
{
    return (int)syscall3(28, (int64_t)addr, (int64_t)length, (int64_t)advice);
}

/* ------------------------------------------------------------------ */
/* Select / epoll / socket message helpers                             */
/* ------------------------------------------------------------------ */

int select(int nfds, fd_set *readfds, fd_set *writefds, fd_set *exceptfds, struct timeval *timeout)
{
    return (int)syscall5(23, (int64_t)nfds, (int64_t)readfds, (int64_t)writefds, (int64_t)exceptfds, (int64_t)timeout);
}

int pselect6(int nfds, fd_set *readfds, fd_set *writefds, fd_set *exceptfds, const struct timespec *timeout, const void *sigmask)
{
    return (int)syscall6(270, (int64_t)nfds, (int64_t)readfds, (int64_t)writefds, (int64_t)exceptfds, (int64_t)timeout, (int64_t)sigmask);
}

int epoll_create1(int flags)
{
    return (int)syscall1(291, (int64_t)flags);
}

int epoll_ctl(int epfd, int op, int fd, struct epoll_event *event)
{
    return (int)syscall4(233, (int64_t)epfd, (int64_t)op, (int64_t)fd, (int64_t)event);
}

int epoll_wait(int epfd, struct epoll_event *events, int maxevents, int timeout)
{
    return (int)syscall4(232, (int64_t)epfd, (int64_t)events, (int64_t)maxevents, (int64_t)timeout);
}

int socketpair(int domain, int type, int protocol, int sv[2])
{
    return (int)syscall4(53, (int64_t)domain, (int64_t)type, (int64_t)protocol, (int64_t)sv);
}

ssize_t sendmsg(int sockfd, const struct msghdr *msg, int flags)
{
    return (ssize_t)syscall3(46, (int64_t)sockfd, (int64_t)msg, (int64_t)flags);
}

ssize_t recvmsg(int sockfd, struct msghdr *msg, int flags)
{
    return (ssize_t)syscall3(47, (int64_t)sockfd, (int64_t)msg, (int64_t)flags);
}


/* ------------------------------------------------------------------ */
/* String library                                                       */
/* ------------------------------------------------------------------ */

int strcmp(const char *s1, const char *s2)
{
    while (*s1 && (*s1 == *s2)) { s1++; s2++; }
    return *(const unsigned char *)s1 - *(const unsigned char *)s2;
}

int strncmp(const char *s1, const char *s2, size_t n)
{
    while (n-- > 0) {
        if (*s1 != *s2) return *(const unsigned char *)s1 - *(const unsigned char *)s2;
        if (*s1 == '\0') return 0;
        s1++; s2++;
    }
    return 0;
}

void strcpy(char *dest, const char *src)
{
    while ((*dest++ = *src++)) {}
}

void strncpy(char *dest, const char *src, size_t n)
{
    size_t i;
    for (i = 0; i < n && src[i] != '\0'; i++) dest[i] = src[i];
    for (; i < n; i++) dest[i] = '\0';
}

char *strcat(char *dest, const char *src)
{
    char *d = dest;
    while (*d) d++;
    while ((*d++ = *src++)) {}
    return dest;
}

size_t strlen(const char *text)
{
    size_t len = 0;
    while (text[len] != '\0') len++;
    return len;
}

char *strchr(const char *s, int c)
{
    while (*s != '\0') {
        if (*s == (char)c) return (char *)s;
        s++;
    }
    return (c == '\0') ? (char *)s : 0;
}

char *strstr(const char *haystack, const char *needle)
{
    if (!haystack || !needle) return 0;
    if (needle[0] == '\0') return (char *)haystack;
    size_t nlen = strlen(needle);
    for (const char *p = haystack; *p; p++) {
        if (strncmp(p, needle, nlen) == 0 && p[nlen] == 0) {
            return (char *)p;
        }
    }
    return 0;
}

void *memset(void *dest, int value, size_t count)
{
    unsigned char *d = dest;
    for (size_t i = 0; i < count; i++) d[i] = (unsigned char)value;
    return dest;
}

void *memcpy(void *dest, const void *src, size_t count)
{
    unsigned char *d = dest;
    const unsigned char *s = src;
    for (size_t i = 0; i < count; i++) d[i] = s[i];
    return dest;
}

int memcmp(const void *s1, const void *s2, size_t n)
{
    const unsigned char *a = s1, *b = s2;
    for (size_t i = 0; i < n; i++) {
        if (a[i] != b[i]) return a[i] - b[i];
    }
    return 0;
}

void *memmove(void *dest, const void *src, size_t count)
{
    unsigned char *d = dest;
    const unsigned char *s = src;
    if (d < s) {
        for (size_t i = 0; i < count; i++) d[i] = s[i];
    } else {
        for (size_t i = count; i > 0; i--) d[i - 1] = s[i - 1];
    }
    return dest;
}

void _exit(int status)
{
    /* SYS_exit_group is preferred for "process and all threads
     * terminate" but our libc-lite is single-threaded per process, so
     * SYS_exit suffices. */
    syscall1(60, (int64_t)status);
    for (;;) __asm__ volatile ("hlt");
}

/* ------------------------------------------------------------------ */
/* printf / snprintf                                                    */
/* ------------------------------------------------------------------ */

static void print_string(const char *s)
{
    write(1, s, strlen(s));
}

static int print_number_to_buf(char *out, size_t out_size, size_t *pos,
                                unsigned long long val, int base, bool is_neg)
{
    char tmp[32];
    int i = 0;
    if (val == 0) {
        tmp[i++] = '0';
    } else {
        while (val > 0) {
            int digit = val % base;
            tmp[i++] = (digit < 10) ? ('0' + digit) : ('a' + digit - 10);
            val /= base;
        }
    }
    if (is_neg) {
        if (out && *pos < out_size - 1) {
            out[(*pos)++] = '-';
        } else if (!out) {
            /* printf path: write the sign to stdout directly because
             * the buffer we're accumulating into is NULL. */
            char dash = '-';
            (void)write(1, &dash, 1);
        }
    }
    while (i > 0) {
        i--;
        if (out) {
            if (*pos < out_size - 1) out[(*pos)++] = tmp[i];
        } else {
            write(1, &tmp[i], 1);
        }
    }
    return 0;
}

static int vsnprintf_impl(char *buf, size_t size, const char *fmt, va_list args)
{
    size_t pos = 0;
    const char *p = fmt;

    while (*p) {
        if (*p != '%') {
            if (buf) { if (pos < size - 1) buf[pos++] = *p; }
            else write(1, p, 1);
        } else {
            p++;
            if (*p == '\0') break;
            switch (*p) {
            case '%':
                if (buf) { if (pos < size - 1) buf[pos++] = '%'; }
                else { char c = '%'; write(1, &c, 1); }
                break;
            case 'c': {
                char c = (char)va_arg(args, int);
                if (buf) { if (pos < size - 1) buf[pos++] = c; }
                else write(1, &c, 1);
                break;
            }
            case 's': {
                const char *s = va_arg(args, const char *);
                if (!s) s = "(null)";
                if (buf) { while (*s && pos < size - 1) buf[pos++] = *s++; }
                else print_string(s);
                break;
            }
            case 'd': {
                int val = va_arg(args, int);
                unsigned long long uval = (val < 0) ? (unsigned long long)(-val) : (unsigned long long)val;
                print_number_to_buf(buf, size, &pos, uval, 10, val < 0);
                break;
            }
            case 'u': {
                unsigned int val = va_arg(args, unsigned int);
                print_number_to_buf(buf, size, &pos, val, 10, false);
                break;
            }
            case 'l':
                p++;
                if (*p == 'l') {
                    p++;
                    if (*p == 'd') {
                        long long val = va_arg(args, long long);
                        unsigned long long uval = (val < 0) ? (unsigned long long)(-val) : (unsigned long long)val;
                        print_number_to_buf(buf, size, &pos, uval, 10, val < 0);
                    } else if (*p == 'u') {
                        unsigned long long val = va_arg(args, unsigned long long);
                        print_number_to_buf(buf, size, &pos, val, 10, false);
                    } else if (*p == 'x') {
                        unsigned long long val = va_arg(args, unsigned long long);
                        print_number_to_buf(buf, size, &pos, val, 16, false);
                    }
                } else if (*p == 'd') {
                    long val = va_arg(args, long);
                    unsigned long long uval = (val < 0) ? (unsigned long long)(-val) : (unsigned long long)val;
                    print_number_to_buf(buf, size, &pos, uval, 10, val < 0);
                } else if (*p == 'u') {
                    unsigned long val = va_arg(args, unsigned long);
                    print_number_to_buf(buf, size, &pos, val, 10, false);
                }
                break;
            case 'x': {
                unsigned int val = va_arg(args, unsigned int);
                print_number_to_buf(buf, size, &pos, val, 16, false);
                break;
            }
            case 'o': {
                unsigned int val = va_arg(args, unsigned int);
                print_number_to_buf(buf, size, &pos, val, 8, false);
                break;
            }
            case 'p': {
                void *val = va_arg(args, void *);
                if (buf) {
                    if (pos + 1 < size - 1) { buf[pos++] = '0'; buf[pos++] = 'x'; }
                } else { write(1, "0x", 2); }
                print_number_to_buf(buf, size, &pos, (unsigned long long)val, 16, false);
                break;
            }
            default:
                break;
            }
        }
        p++;
    }

    if (buf && size > 0) buf[pos] = '\0';
    return (int)pos;
}

int printf(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    int ret = vsnprintf_impl(0, (size_t)-1, fmt, args);
    va_end(args);
    return ret;
}

int snprintf(char *buf, size_t size, const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    int ret = vsnprintf_impl(buf, size, fmt, args);
    va_end(args);
    return ret;
}

int pipe(int pipefd[2])
{
    return (int)syscall1(22, (int64_t)pipefd);
}

int pipe2(int pipefd[2], int flags)
{
    return (int)syscall2(293, (int64_t)pipefd, (int64_t)flags);
}

int poll(struct pollfd *fds, unsigned long nfds, int timeout)
{
    return (int)syscall3(7, (int64_t)fds, (int64_t)nfds, (int64_t)timeout);
}

int clock_gettime(int clockid, struct timespec *tp)
{
    return (int)syscall2(228, (int64_t)clockid, (int64_t)tp);
}

int gettimeofday(struct timeval *tv, struct timezone *tz)
{
    return (int)syscall2(96, (int64_t)tv, (int64_t)tz);
}

int nanosleep(const struct timespec *req, struct timespec *rem)
{
    return (int)syscall2(35, (int64_t)req, (int64_t)rem);
}

unsigned int sleep(unsigned int seconds)
{
    struct timespec req;
    req.tv_sec = seconds;
    req.tv_nsec = 0;
    nanosleep(&req, 0);
    return 0;
}

int socket(int domain, int type, int protocol)
{
    return (int)syscall3(41, (int64_t)domain, (int64_t)type, (int64_t)protocol);
}

int bind(int sockfd, const struct sockaddr *addr, socklen_t addrlen)
{
    return (int)syscall3(49, (int64_t)sockfd, (int64_t)addr, (int64_t)addrlen);
}

int listen(int sockfd, int backlog)
{
    return (int)syscall2(50, (int64_t)sockfd, (int64_t)backlog);
}

int accept(int sockfd, struct sockaddr *addr, socklen_t *addrlen)
{
    return (int)syscall3(43, (int64_t)sockfd, (int64_t)addr, (int64_t)addrlen);
}

int connect(int sockfd, const struct sockaddr *addr, socklen_t addrlen)
{
    return (int)syscall3(42, (int64_t)sockfd, (int64_t)addr, (int64_t)addrlen);
}

ssize_t sendto(int sockfd, const void *buf, size_t len, int flags, const struct sockaddr *dest_addr, socklen_t addrlen)
{
    return (ssize_t)syscall6(44, (int64_t)sockfd, (int64_t)buf, (int64_t)len, (int64_t)flags, (int64_t)dest_addr, (int64_t)addrlen);
}

ssize_t recvfrom(int sockfd, void *buf, size_t len, int flags, struct sockaddr *src_addr, socklen_t *addrlen)
{
    return (ssize_t)syscall6(45, (int64_t)sockfd, (int64_t)buf, (int64_t)len, (int64_t)flags, (int64_t)src_addr, (int64_t)addrlen);
}

int reboot(int cmd)
{
    return (int)syscall3(169, 0xfee1dead, 672274793, (int64_t)cmd);
}

uint32_t getuid(void)  { return (uint32_t)syscall0(102); }
uint32_t getgid(void)  { return (uint32_t)syscall0(104); }
uint32_t geteuid(void) { return (uint32_t)syscall0(107); }
uint32_t getegid(void) { return (uint32_t)syscall0(108); }

/* ------------------------------------------------------------------ */
/* Number parsing                                                       */
/* ------------------------------------------------------------------ */

static int is_space(int c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f'; }

unsigned long strtoul(const char *s, char **endptr, int base)
{
    unsigned long v = 0;
    while (is_space((unsigned char)*s)) s++;
    if (base == 0) {
        if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) { base = 16; s += 2; }
        else if (s[0] == '0') { base = 8; s++; }
        else base = 10;
    } else if (base == 16 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        s += 2;
    }
    while (*s) {
        int d;
        if (*s >= '0' && *s <= '9') d = *s - '0';
        else if (*s >= 'a' && *s <= 'z') d = *s - 'a' + 10;
        else if (*s >= 'A' && *s <= 'Z') d = *s - 'A' + 10;
        else break;
        if (d >= base) break;
        v = v * (unsigned long)base + (unsigned long)d;
        s++;
    }
    if (endptr) *endptr = (char *)s;
    return v;
}

long strtol(const char *s, char **endptr, int base)
{
    int neg = 0;
    while (is_space((unsigned char)*s)) s++;
    if (*s == '+') s++;
    else if (*s == '-') { neg = 1; s++; }
    unsigned long v = strtoul(s, endptr, base);
    return neg ? -(long)v : (long)v;
}

int atoi(const char *s)
{
    return (int)strtol(s, 0, 10);
}

char *strsep_inplace(char **stringp, int delim)
{
    if (!stringp || !*stringp) return 0;
    char *start = *stringp;
    char *p = start;
    while (*p && *p != delim) p++;
    if (*p == delim) { *p = '\0'; *stringp = p + 1; }
    else { *stringp = 0; }
    return start;
}

/* ------------------------------------------------------------------ */
/* read_line: read until '\n' or EOF or buf-full                       */
/* ------------------------------------------------------------------ */

ssize_t read_line(int fd, char *buf, size_t size)
{
    if (size == 0) return 0;
    size_t pos = 0;
    while (pos + 1 < size) {
        char c;
        ssize_t n = read(fd, &c, 1);
        if (n <= 0) {
            if (pos == 0 && n <= 0) return (n < 0) ? -1 : (ssize_t)pos;
            break;
        }
        if (c == '\n') break;
        if (c == '\r') continue;
        buf[pos++] = c;
    }
    buf[pos] = '\0';
    return (ssize_t)pos;
}

/* ------------------------------------------------------------------ */
/* SHA-256                                                              */
/* ------------------------------------------------------------------ */

static const uint32_t sha256_k[64] = {
    0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
    0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
    0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
    0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
    0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
    0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
    0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
    0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2
};

static uint32_t rotr32(uint32_t x, unsigned n) { return (x >> n) | (x << (32 - n)); }

static void sha256_compress(uint32_t state[8], const uint8_t block[64])
{
    uint32_t w[64];
    for (int i = 0; i < 16; i++) {
        w[i] = ((uint32_t)block[i*4] << 24) | ((uint32_t)block[i*4+1] << 16)
             | ((uint32_t)block[i*4+2] << 8) | (uint32_t)block[i*4+3];
    }
    for (int i = 16; i < 64; i++) {
        uint32_t s0 = rotr32(w[i-15], 7) ^ rotr32(w[i-15], 18) ^ (w[i-15] >> 3);
        uint32_t s1 = rotr32(w[i-2], 17) ^ rotr32(w[i-2], 19) ^ (w[i-2] >> 10);
        w[i] = w[i-16] + s0 + w[i-7] + s1;
    }
    uint32_t a=state[0],b=state[1],c=state[2],d=state[3],e=state[4],f=state[5],g=state[6],h=state[7];
    for (int i = 0; i < 64; i++) {
        uint32_t S1 = rotr32(e, 6) ^ rotr32(e, 11) ^ rotr32(e, 25);
        uint32_t ch = (e & f) ^ ((~e) & g);
        uint32_t t1 = h + S1 + ch + sha256_k[i] + w[i];
        uint32_t S0 = rotr32(a, 2) ^ rotr32(a, 13) ^ rotr32(a, 22);
        uint32_t mj = (a & b) ^ (a & c) ^ (b & c);
        uint32_t t2 = S0 + mj;
        h = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
    }
    state[0]+=a; state[1]+=b; state[2]+=c; state[3]+=d;
    state[4]+=e; state[5]+=f; state[6]+=g; state[7]+=h;
}

void sha256(const void *data, size_t len, uint8_t out32[32])
{
    uint32_t state[8] = {
        0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,
        0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19
    };
    const uint8_t *p = (const uint8_t *)data;
    size_t full_blocks = len / 64;
    for (size_t i = 0; i < full_blocks; i++) sha256_compress(state, p + i * 64);

    uint8_t tail[128];
    size_t rem = len - full_blocks * 64;
    memcpy(tail, p + full_blocks * 64, rem);
    tail[rem] = 0x80;
    size_t pad_len = (rem + 1 <= 56) ? 64 : 128;
    for (size_t i = rem + 1; i < pad_len - 8; i++) tail[i] = 0;
    uint64_t bits = (uint64_t)len * 8;
    for (int i = 0; i < 8; i++) tail[pad_len - 1 - i] = (uint8_t)(bits >> (i * 8));
    sha256_compress(state, tail);
    if (pad_len == 128) sha256_compress(state, tail + 64);

    for (int i = 0; i < 8; i++) {
        out32[i*4]   = (uint8_t)(state[i] >> 24);
        out32[i*4+1] = (uint8_t)(state[i] >> 16);
        out32[i*4+2] = (uint8_t)(state[i] >> 8);
        out32[i*4+3] = (uint8_t)(state[i]);
    }
}

/* ------------------------------------------------------------------ */
/* Password hashing                                                     */
/* Hash format: $5$<salt>$<hex64>                                      */
/* Implementation: SHA-256( salt || password ) iterated 5000 times      */
/* (Simplified vs glibc crypt — sufficient for distro-level use.)       */
/* ------------------------------------------------------------------ */

static const char hex_chars[] = "0123456789abcdef";

void pw_hash(const char *password, const char *salt, char *out, size_t out_size)
{
    if (out_size < 16) { if (out_size) out[0] = 0; return; }

    /* Cap salt length so the per-round feed buffer can never overflow. */
    size_t slen = strlen(salt);
    if (slen > 64) slen = 64;
    size_t plen = strlen(password);
    if (plen > 128) plen = 128;

    uint8_t buf[256];
    memcpy(buf, salt, slen);
    memcpy(buf + slen, password, plen);

    uint8_t digest[32];
    sha256(buf, slen + plen, digest);
    for (int round = 0; round < 4999; round++) {
        uint8_t feed[32 + 64];
        memcpy(feed, digest, 32);
        memcpy(feed + 32, salt, slen);
        sha256(feed, 32 + slen, digest);
    }

    char hex[65];
    for (int i = 0; i < 32; i++) {
        hex[i*2]   = hex_chars[(digest[i] >> 4) & 0xF];
        hex[i*2+1] = hex_chars[digest[i] & 0xF];
    }
    hex[64] = 0;

    /* Build the capped salt copy for the formatted output. */
    char salt_capped[65];
    memcpy(salt_capped, salt, slen);
    salt_capped[slen] = 0;
    snprintf(out, out_size, "$5$%s$%s", salt_capped, hex);
}

int pw_verify(const char *password, const char *stored_hash)
{
    if (!stored_hash || stored_hash[0] == 0) {
        /* empty hash = no password required */
        return password[0] == 0 ? 1 : 0;
    }
    if (stored_hash[0] == '!' || stored_hash[0] == '*') {
        /* account locked */
        return 0;
    }
    if (strncmp(stored_hash, "$5$", 3) != 0) {
        /* legacy / unsupported - reject */
        return 0;
    }
    /* extract salt between $5$ and the next $ */
    const char *salt_start = stored_hash + 3;
    const char *salt_end = strchr(salt_start, '$');
    if (!salt_end) return 0;
    size_t salt_len = (size_t)(salt_end - salt_start);
    if (salt_len == 0 || salt_len > 64) return 0;

    char salt[65];
    memcpy(salt, salt_start, salt_len);
    salt[salt_len] = 0;

    char recomputed[160];
    pw_hash(password, salt, recomputed, sizeof(recomputed));
    return strcmp(recomputed, stored_hash) == 0 ? 1 : 0;
}

/* ------------------------------------------------------------------ */
/* /etc/passwd and /etc/shadow parsing                                  */
/* ------------------------------------------------------------------ */

static int parse_passwd_line(char *line, struct passwd_ent *out)
{
    char *p = line;
    char *name = strsep_inplace(&p, ':');
    char *pw   = strsep_inplace(&p, ':');
    char *uid  = strsep_inplace(&p, ':');
    char *gid  = strsep_inplace(&p, ':');
    char *gec  = strsep_inplace(&p, ':');
    char *hom  = strsep_inplace(&p, ':');
    char *shl  = p;
    if (!name || !pw || !uid || !gid || !hom || !shl) return -1;

    memset(out, 0, sizeof(*out));
    strncpy(out->name, name, sizeof(out->name) - 1);
    strncpy(out->passwd_field, pw, sizeof(out->passwd_field) - 1);
    out->uid = (uint32_t)atoi(uid);
    out->gid = (uint32_t)atoi(gid);
    if (gec) strncpy(out->gecos, gec, sizeof(out->gecos) - 1);
    strncpy(out->home, hom, sizeof(out->home) - 1);
    strncpy(out->shell, shl, sizeof(out->shell) - 1);
    return 0;
}

static int passwd_iter(int (*pred)(const struct passwd_ent *, void *), void *ctx, struct passwd_ent *out)
{
    int fd = open("/etc/passwd", O_RDONLY);
    if (fd < 0) return -1;
    /* Use static buffer to avoid stack/uaccess edge cases with large user reads */
    static char buf[2048];
    ssize_t n = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (n <= 0) return -1;
    buf[n] = 0;

    char *line = buf;
    while (*line) {
        char *nl = strchr(line, '\n');
        if (nl) *nl = 0;
        if (line[0] && line[0] != '#') {
            char tmp[512];
            strncpy(tmp, line, sizeof(tmp) - 1);
            tmp[sizeof(tmp) - 1] = 0;
            struct passwd_ent ent;
            if (parse_passwd_line(tmp, &ent) == 0) {
                if (pred(&ent, ctx)) {
                    *out = ent;
                    return 0;
                }
            }
        }
        if (!nl) break;
        line = nl + 1;
    }
    return -1;
}

static int pred_by_name(const struct passwd_ent *e, void *ctx) { return strcmp(e->name, (const char *)ctx) == 0; }
static int pred_by_uid(const struct passwd_ent *e, void *ctx) { return e->uid == *(uint32_t *)ctx; }

int pwent_by_name(const char *name, struct passwd_ent *out)
{
    return passwd_iter(pred_by_name, (void *)name, out);
}

int pwent_by_uid(uint32_t uid, struct passwd_ent *out)
{
    return passwd_iter(pred_by_uid, &uid, out);
}

static int parse_shadow_line(char *line, struct shadow_ent *out)
{
    char *p = line;
    char *name = strsep_inplace(&p, ':');
    char *hash = strsep_inplace(&p, ':');
    if (!name || !hash) return -1;
    memset(out, 0, sizeof(*out));
    strncpy(out->name, name, sizeof(out->name) - 1);
    strncpy(out->hash, hash, sizeof(out->hash) - 1);

    char *f;
    f = strsep_inplace(&p, ':'); if (f && *f) out->last_change  = (uint64_t)strtoul(f, 0, 10);
    f = strsep_inplace(&p, ':'); if (f && *f) out->min_age      = (uint64_t)strtoul(f, 0, 10);
    f = strsep_inplace(&p, ':'); if (f && *f) out->max_age      = (uint64_t)strtoul(f, 0, 10);
    f = strsep_inplace(&p, ':'); if (f && *f) out->warn_period  = (uint64_t)strtoul(f, 0, 10);
    f = strsep_inplace(&p, ':'); if (f && *f) out->inactive     = (uint64_t)strtoul(f, 0, 10);
    f = strsep_inplace(&p, ':'); if (f && *f) out->expire       = (uint64_t)strtoul(f, 0, 10);
    return 0;
}

int shent_by_name(const char *name, struct shadow_ent *out)
{
    int fd = open("/etc/shadow", O_RDONLY);
    if (fd < 0) return -1;
    static char buf[2048];
    ssize_t n = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (n <= 0) return -1;
    buf[n] = 0;

    char *line = buf;
    while (*line) {
        char *nl = strchr(line, '\n');
        if (nl) *nl = 0;
        if (line[0] && line[0] != '#') {
            char tmp[1024];
            strncpy(tmp, line, sizeof(tmp) - 1);
            tmp[sizeof(tmp) - 1] = 0;
            struct shadow_ent ent;
            if (parse_shadow_line(tmp, &ent) == 0 && strcmp(ent.name, name) == 0) {
                *out = ent;
                return 0;
            }
        }
        if (!nl) break;
        line = nl + 1;
    }
    return -1;
}

int shent_set_hash(const char *name, const char *new_hash)
{
    int fd = open("/etc/shadow", O_RDONLY);
    if (fd < 0) return -1;
    static char buf[2048];
    ssize_t n = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (n <= 0) return -1;
    buf[n] = 0;

    static char out_buf[2048];
    size_t op = 0;
    int replaced = 0;
    char *line = buf;
    while (*line) {
        char *nl = strchr(line, '\n');
        if (nl) *nl = 0;
        if (line[0] && line[0] != '#') {
            char tmp[1024];
            strncpy(tmp, line, sizeof(tmp) - 1);
            tmp[sizeof(tmp) - 1] = 0;
            char *p = tmp;
            char *e_name = strsep_inplace(&p, ':');
            (void)strsep_inplace(&p, ':');         /* old hash discarded */
            char *rest = p ? p : (char *)"";       /* remainder of line */
            if (e_name && strcmp(e_name, name) == 0) {
                op += snprintf(out_buf + op, sizeof(out_buf) - op, "%s:%s:%s\n", e_name, new_hash, rest);
                replaced = 1;
            } else {
                /* reassemble untouched */
                op += snprintf(out_buf + op, sizeof(out_buf) - op, "%s\n", line);
            }
        } else if (line[0]) {
            op += snprintf(out_buf + op, sizeof(out_buf) - op, "%s\n", line);
        } else {
            op += snprintf(out_buf + op, sizeof(out_buf) - op, "\n");
        }
        if (!nl) break;
        line = nl + 1;
    }
    if (!replaced) {
        op += snprintf(out_buf + op, sizeof(out_buf) - op, "%s:%s:0:0:99999:7:::\n", name, new_hash);
    }

    /* Atomic-ish: write to /etc/shadow.new then rename. We preserve the
     * original /etc/shadow mode/ownership so the file does not become
     * world-readable after the update. */
    struct stat orig_st;
    int have_orig = (stat("/etc/shadow", &orig_st) == 0);

    int wfd = open("/etc/shadow.new", O_WRONLY | O_CREAT | O_TRUNC);
    if (wfd < 0) return -1;
    ssize_t w = write(wfd, out_buf, op);
    close(wfd);
    if (w != (ssize_t)op) { unlink("/etc/shadow.new"); return -1; }

    /* Match the original file's mode bits before swapping in. */
    if (have_orig) {
        chmod_libc("/etc/shadow.new", orig_st.st_mode & 07777);
    } else {
        chmod_libc("/etc/shadow.new", 0600);
    }

    if (rename("/etc/shadow.new", "/etc/shadow") != 0) {
        /* Fallback to direct write if rename unsupported on this fs */
        int dfd = open("/etc/shadow", O_WRONLY | O_CREAT | O_TRUNC);
        if (dfd < 0) return -1;
        ssize_t w2 = write(dfd, out_buf, op);
        close(dfd);
        unlink("/etc/shadow.new");
        if (w2 != (ssize_t)op) return -1;
        if (have_orig) chmod_libc("/etc/shadow", orig_st.st_mode & 07777);
        else chmod_libc("/etc/shadow", 0600);
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* Terminal echo control                                                */
/* ------------------------------------------------------------------ */

#define ECHO_FLAG    0x00000008
#define ICANON_FLAG  0x00000002

int term_echo_off(int fd, struct termios *saved)
{
    if (!saved) return -1;
    if (ioctl(fd, TCGETS, saved) != 0) return -1;
    struct termios t = *saved;
    t.c_lflag &= ~(ECHO_FLAG);
    return ioctl(fd, TCSETS, &t);
}

int term_echo_restore(int fd, const struct termios *saved)
{
    if (!saved) return -1;
    return ioctl(fd, TCSETS, (struct termios *)saved);
}

/* ------------------------------------------------------------------ */
/* Salt generation: 8 URL-safe chars from /dev/urandom or getrandom    */
/* ------------------------------------------------------------------ */

void gen_salt(char *out, size_t size)
{
    static const char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789./";
    if (size == 0) return;
    size_t want = size - 1;
    if (want > 16) want = 16;
    uint8_t rnd[16];
    ssize_t n = getrandom(rnd, want, 0);
    if (n < (ssize_t)want) {
        /* Fallback to a counter — not secure, but deterministic */
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        for (size_t i = 0; i < want; i++) rnd[i] = (uint8_t)(ts.tv_nsec >> (i * 4));
    }
    for (size_t i = 0; i < want; i++) out[i] = alphabet[rnd[i] & 0x3F];
    out[want] = 0;
}

int dhcp_parse_reply(const void *pkt, size_t pkt_len,
                     const void *opts, size_t opts_len,
                     uint32_t expected_xid, struct dhcp_lease *out_lease)
{
    if (!pkt || pkt_len < sizeof(struct dhcp_packet_hdr)) return -1;
    const struct dhcp_packet_hdr *h = (const struct dhcp_packet_hdr *)pkt;
    if (h->op != DHCP_OP_BOOTREPLY) return -1;
    if (h->magic != DHCP_MAGIC_COOKIE) return -1;
    if (h->xid != expected_xid) return -1;

    memset(out_lease, 0, sizeof(*out_lease));
    memcpy(out_lease->your_ip, h->yiaddr, 4);

    const uint8_t *p = (const uint8_t *)opts;
    size_t i = 0;
    while (i < opts_len) {
        uint8_t code = p[i++];
        if (code == DHCP_OPT_END) break;
        if (i >= opts_len) break;
        uint8_t len = p[i++];
        if (i + len > opts_len) break;

        switch (code) {
        case DHCP_OPT_MSG_TYPE:
            if (len >= 1) out_lease->msg_type = p[i];
            break;
        case DHCP_OPT_SERVER_ID:
            if (len == 4) { memcpy(out_lease->server_id, p + i, 4); out_lease->has_server_id = true; }
            break;
        case DHCP_OPT_SUBNET_MASK:
            if (len == 4) { memcpy(out_lease->subnet_mask, p + i, 4); out_lease->has_subnet_mask = true; }
            break;
        case DHCP_OPT_ROUTER:
            if (len >= 4) { memcpy(out_lease->router, p + i, 4); out_lease->has_router = true; }
            break;
        case DHCP_OPT_DNS:
            if (len >= 4) { memcpy(out_lease->dns, p + i, 4); out_lease->has_dns = true; }
            break;
        case DHCP_OPT_LEASE_TIME:
            if (len == 4) {
                out_lease->lease_seconds = ((uint32_t)p[i]   << 24) | ((uint32_t)p[i+1] << 16) |
                                            ((uint32_t)p[i+2] << 8)  |  (uint32_t)p[i+3];
            }
            break;
        case DHCP_OPT_RENEWAL:
            if (len == 4) {
                out_lease->t1_seconds = ((uint32_t)p[i]   << 24) | ((uint32_t)p[i+1] << 16) |
                                          ((uint32_t)p[i+2] << 8)  |  (uint32_t)p[i+3];
            }
            break;
        case DHCP_OPT_REBIND:
            if (len == 4) {
                out_lease->t2_seconds = ((uint32_t)p[i]   << 24) | ((uint32_t)p[i+1] << 16) |
                                          ((uint32_t)p[i+2] << 8)  |  (uint32_t)p[i+3];
            }
            break;
        case DHCP_OPT_HOSTNAME:
            if (len < sizeof(out_lease->hostname)) {
                memcpy(out_lease->hostname, p + i, len);
                out_lease->hostname[len] = 0;
                out_lease->has_hostname = true;
            }
            break;
        case DHCP_OPT_DOMAIN_NAME:
            if (len < sizeof(out_lease->domain)) {
                memcpy(out_lease->domain, p + i, len);
                out_lease->domain[len] = 0;
                out_lease->has_domain = true;
            }
            break;
        }
        i += len;
    }
    return 0;
}

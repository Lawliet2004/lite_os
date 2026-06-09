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
        if (out && *pos < out_size - 1) out[(*pos)++] = '-';
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

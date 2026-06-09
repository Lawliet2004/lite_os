#ifndef LIBC_LITE_H
#define LIBC_LITE_H

#include <stddef.h>
#include <stdint.h>
#include <stdarg.h>

typedef int64_t ssize_t;
typedef int64_t off_t;

/* open flags — must match kernel's vfs.h */
#define O_RDONLY    0
#define O_WRONLY    1
#define O_RDWR      2
#define O_CREAT     64
#define O_TRUNC     512
#define O_APPEND    1024
#define O_NONBLOCK  2048
#define O_CLOEXEC   524288
#define O_DIRECTORY 65536

#define AT_FDCWD    -100
#define AT_EMPTY_PATH 0x1000

#define F_OK 0
#define X_OK 1
#define W_OK 2
#define R_OK 4

#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

/* Directory entry types */
#define DT_UNKNOWN  0
#define DT_FIFO     1
#define DT_CHR      2
#define DT_DIR      4
#define DT_BLK      6
#define DT_REG      8
#define DT_LNK      10
#define DT_SOCK     12

/* wait4 options */
#define WNOHANG     1
#define WUNTRACED   2

/* wait status decoding — Linux ABI */
#define WIFEXITED(s)    (((s) & 0x7f) == 0)
#define WEXITSTATUS(s)  (((s) >> 8) & 0xff)
#define WIFSIGNALED(s)  (((s) & 0x7f) != 0 && ((s) & 0x7f) != 0x7f)
#define WTERMSIG(s)     ((s) & 0x7f)

/* Signals */
#define SIGHUP     1
#define SIGINT     2
#define SIGQUIT    3
#define SIGILL     4
#define SIGTRAP    5
#define SIGABRT    6
#define SIGIOT     6
#define SIGBUS     7
#define SIGFPE     8
#define SIGKILL    9
#define SIGUSR1    10
#define SIGSEGV    11
#define SIGUSR2    12
#define SIGPIPE    13
#define SIGALRM    14
#define SIGTERM    15
#define SIGSTKFLT  16
#define SIGCHLD    17
#define SIGCONT    18
#define SIGSTOP    19
#define SIGTSTP    20
#define SIGTTIN    21
#define SIGTTOU    22
#define SIGURG     23
#define SIGXCPU    24
#define SIGXFSZ    25
#define SIGVTALRM  26
#define SIGPROF    27
#define SIGWINCH   28
#define SIGIO      29
#define SIGPOLL    SIGIO
#define SIGPWR     30
#define SIGSYS     31

#define SIG_DFL    ((void (*)(int))0)
#define SIG_IGN    ((void (*)(int))1)
#define SIG_ERR    ((void (*)(int))-1)

#define SIG_BLOCK   0
#define SIG_UNBLOCK 1
#define SIG_SETMASK 2

/* ioctl numbers */
#define TCGETS      0x5401
#define TCSETS      0x5402
#define TCSETSW     0x5403
#define TCSETSF     0x5404
#define TIOCGWINSZ  0x5413
#define TIOCSWINSZ  0x5414
#define TIOCGPGRP   0x540F
#define TIOCSPGRP   0x5410

struct termios {
    uint32_t c_iflag;
    uint32_t c_oflag;
    uint32_t c_cflag;
    uint32_t c_lflag;
    uint8_t  c_line;
    uint8_t  c_cc[32];
};

struct winsize {
    unsigned short ws_row;
    unsigned short ws_col;
    unsigned short ws_xpixel;
    unsigned short ws_ypixel;
};

struct stat {
    uint64_t st_dev;
    uint64_t st_ino;
    uint64_t st_nlink;
    uint32_t st_mode;
    uint32_t st_uid;
    uint32_t st_gid;
    uint32_t __pad0;
    uint64_t st_rdev;
    int64_t  st_size;
    int64_t  st_blksize;
    int64_t  st_blocks;
    int64_t  st_atime;
    uint64_t st_atime_nsec;
    int64_t  st_mtime;
    uint64_t st_mtime_nsec;
    int64_t  st_ctime;
    uint64_t st_ctime_nsec;
    int64_t  __unused[3];
};

struct linux_dirent64 {
    uint64_t d_ino;
    int64_t  d_off;
    uint16_t d_reclen;
    uint8_t  d_type;
    char     d_name[];
};

struct utsname {
    char sysname[65];
    char nodename[65];
    char release[65];
    char version[65];
    char machine[65];
    char domainname[65];
};

struct statx_timestamp {
    int64_t tv_sec;
    uint32_t tv_nsec;
    int32_t __reserved;
};

struct statx {
    uint32_t stx_mask;
    uint32_t stx_blksize;
    uint64_t stx_attributes;
    uint32_t stx_nlink;
    uint32_t stx_uid;
    uint32_t stx_gid;
    uint16_t stx_mode;
    uint16_t __spare0[1];
    uint64_t stx_ino;
    uint64_t stx_size;
    uint64_t stx_blocks;
    uint64_t stx_attributes_mask;
    struct statx_timestamp stx_atime;
    struct statx_timestamp stx_btime;
    struct statx_timestamp stx_ctime;
    struct statx_timestamp stx_mtime;
    uint32_t stx_rdev_major;
    uint32_t stx_rdev_minor;
    uint32_t stx_dev_major;
    uint32_t stx_dev_minor;
    uint64_t __spare2[14];
};

struct rlimit {
    uint64_t rlim_cur;
    uint64_t rlim_max;
};

#define RLIMIT_STACK  3
#define RLIMIT_NOFILE 7
#define RLIMIT_AS     9

/* ------------------------------------------------------------------ */
/* Syscall wrappers                                                     */
/* ------------------------------------------------------------------ */

/* File I/O */
int open(const char *pathname, int flags);
int openat(int dirfd, const char *pathname, int flags, int mode);
int close(int fd);
ssize_t read(int fd, void *buf, size_t count);
ssize_t write(int fd, const void *buf, size_t count);
off_t lseek(int fd, off_t offset, int whence);
int fstat(int fd, struct stat *statbuf);
int stat(const char *pathname, struct stat *statbuf);
int fstatat(int dirfd, const char *pathname, struct stat *statbuf, int flags);
int statx(int dirfd, const char *pathname, int flags, unsigned int mask, struct statx *statxbuf);
int access(const char *pathname, int mode);
int faccessat(int dirfd, const char *pathname, int mode, int flags);
ssize_t readlink(const char *pathname, char *buf, size_t bufsiz);
ssize_t readlinkat(int dirfd, const char *pathname, char *buf, size_t bufsiz);
int getdents64(unsigned int fd, struct linux_dirent64 *dirp, unsigned int count);
int ioctl(int fd, unsigned long request, void *argp);
int dup(int fd);
int dup2(int oldfd, int newfd);

/* Directory / FS */
int mkdir(const char *pathname, int mode);
int rmdir(const char *pathname);
int unlink(const char *pathname);
int rename(const char *oldpath, const char *newpath);
int chdir(const char *path);
char *getcwd(char *buf, size_t size);
int symlink(const char *target, const char *linkpath);

/* Process */
int execve(const char *pathname, char *const argv[], char *const envp[]);
void exit(int status) __attribute__((noreturn));
int fork(void);
int wait4(int pid, int *wstatus, int options, void *rusage);
int getpid(void);
int gettid(void);
int getppid(void);
int setpgid(int pid, int pgid);
int getpgid(int pid);
int setsid(void);
int getsid(int pid);
int clone(unsigned long flags, void *child_stack, int *parent_tid, int *child_tid, unsigned long newtls);
int clone_thread(int (*fn)(void *), void *child_stack, void *arg);
int futex(uint32_t *uaddr, int op, uint32_t val, const void *timeout, uint32_t *uaddr2, uint32_t val3);
int arch_prctl(int code, unsigned long addr);
int set_tid_address(int *tidptr);
int set_robust_list(void *head, size_t len);
int get_robust_list(int pid, void **head, size_t *len);

struct sigaction {
    void (*sa_handler)(int);
    unsigned long sa_flags;
    void (*sa_restorer)(void);
    uint64_t sa_mask;
};

int sigaction(int signum, const struct sigaction *act, struct sigaction *oldact);
int sigprocmask(int how, const uint64_t *set, uint64_t *oldset);
int kill(int pid, int sig);
int uname(struct utsname *buf);
ssize_t getrandom(void *buf, size_t buflen, unsigned int flags);
int getrlimit(int resource, struct rlimit *rlim);
int prlimit64(int pid, int resource, const struct rlimit *new_limit, struct rlimit *old_limit);

#define CLONE_VM             0x00000100
#define CLONE_FS             0x00000200
#define CLONE_FILES          0x00000400
#define CLONE_SIGHAND        0x00000800
#define CLONE_THREAD         0x00010000
#define CLONE_SETTLS         0x00080000
#define CLONE_PARENT_SETTID  0x00100000
#define CLONE_CHILD_CLEARTID 0x00200000
#define CLONE_CHILD_SETTID   0x01000000

#define FUTEX_WAIT           0
#define FUTEX_WAKE           1
#define FUTEX_PRIVATE_FLAG   128

#define ARCH_SET_FS          0x1002
#define ARCH_GET_FS          0x1003

struct timespec {
    int64_t tv_sec;
    int64_t tv_nsec;
};

struct timeval {
    int64_t tv_sec;
    int64_t tv_usec;
};

struct timezone {
    int tz_minuteswest;
    int tz_dsttime;
};

struct pollfd {
    int fd;
    short events;
    short revents;
};

#define POLLIN     0x0001
#define POLLOUT    0x0004
#define POLLERR    0x0008
#define POLLHUP    0x0010
#define POLLNVAL   0x0020

#define CLOCK_REALTIME  0
#define CLOCK_MONOTONIC 1

int pipe(int pipefd[2]);
int pipe2(int pipefd[2], int flags);
int poll(struct pollfd *fds, unsigned long nfds, int timeout);
int clock_gettime(int clockid, struct timespec *tp);
int gettimeofday(struct timeval *tv, struct timezone *tz);
int nanosleep(const struct timespec *req, struct timespec *rem);
unsigned int sleep(unsigned int seconds);

/* Memory */
void *brk_syscall(unsigned long addr);
void *mmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset);
int munmap(void *addr, size_t length);

#define PROT_NONE  0
#define PROT_READ  1
#define PROT_WRITE 2
#define PROT_EXEC  4

#define MAP_SHARED    0x01
#define MAP_PRIVATE   0x02
#define MAP_FIXED     0x10
#define MAP_ANONYMOUS 0x20
#define MAP_ANON      MAP_ANONYMOUS
#define MAP_FAILED    ((void *)-1)


/* String/Utility library */
int strcmp(const char *s1, const char *s2);
int strncmp(const char *s1, const char *s2, size_t n);
void strcpy(char *dest, const char *src);
void strncpy(char *dest, const char *src, size_t n);
char *strcat(char *dest, const char *src);
size_t strlen(const char *text);
char *strchr(const char *s, int c);
void *memset(void *dest, int value, size_t count);
void *memcpy(void *dest, const void *src, size_t count);
int memcmp(const void *s1, const void *s2, size_t n);
int printf(const char *fmt, ...);
int snprintf(char *buf, size_t size, const char *fmt, ...);

typedef uint32_t socklen_t;

struct sockaddr {
    uint16_t sa_family;
    char     sa_data[14];
};

struct in_addr {
    uint32_t s_addr;
};

struct sockaddr_in {
    uint16_t       sin_family;
    uint16_t       sin_port;
    struct in_addr sin_addr;
    char           sin_zero[8];
};

int socket(int domain, int type, int protocol);
int bind(int sockfd, const struct sockaddr *addr, socklen_t addrlen);
int listen(int sockfd, int backlog);
int accept(int sockfd, struct sockaddr *addr, socklen_t *addrlen);
int connect(int sockfd, const struct sockaddr *addr, socklen_t addrlen);
ssize_t sendto(int sockfd, const void *buf, size_t len, int flags, const struct sockaddr *dest_addr, socklen_t addrlen);
ssize_t recvfrom(int sockfd, void *buf, size_t len, int flags, struct sockaddr *src_addr, socklen_t *addrlen);

#endif

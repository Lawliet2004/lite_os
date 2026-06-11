#include "libc_lite.h"

#define PR_SET_NAME 15
#define PR_GET_NAME 16
#define MADV_DONTNEED 4
#define MREMAP_MAYMOVE 1
#define AF_UNIX 1
#define SOCK_STREAM 1
#define EPOLL_CTL_ADD 1
#define EPOLL_CTL_DEL 2
#define SIGUSR1 10
#define EINTR 4

static void fail(const char *msg)
{
    printf("COMPAT_ABI: FAILED: %s\n", msg);
    exit(1);
}

static void require(int cond, const char *msg)
{
    if (!cond) {
        fail(msg);
    }
}

static void nap_ms(int ms)
{
    struct timespec ts;
    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (ms % 1000) * 1000000L;
    nanosleep(&ts, 0);
}

static void zero_fdset(fd_set *set)
{
    memset(set, 0, sizeof(*set));
}

static void set_fd(fd_set *set, int fd)
{
    set->fds_bits[fd / 64] |= (1ULL << (fd % 64));
}

static void ignore_handler(int sig)
{
    (void)sig;
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    printf("COMPAT_ABI: begin\n");

    /* Credentials and prctl */
    uint32_t ruid = 0xdeadbeef, euid = 0xdeadbeef, suid = 0xdeadbeef;
    uint32_t rgid = 0xdeadbeef, egid = 0xdeadbeef, sgid = 0xdeadbeef;
    require(getresuid(&ruid, &euid, &suid) == 0, "getresuid");
    require(getresgid(&rgid, &egid, &sgid) == 0, "getresgid");
    require(ruid == 0 && euid == 0 && suid == 0, "uid values");
    require(rgid == 0 && egid == 0 && sgid == 0, "gid values");
    require(setresuid((uint32_t)-1, (uint32_t)-1, (uint32_t)-1) == 0, "setresuid no-op");
    require(setresgid((uint32_t)-1, (uint32_t)-1, (uint32_t)-1) == 0, "setresgid no-op");

    char name[16];
    memset(name, 0, sizeof(name));
    require(prctl(PR_SET_NAME, (unsigned long)"compat_abi", 0, 0, 0) == 0, "prctl PR_SET_NAME");
    require(prctl(PR_GET_NAME, (unsigned long)name, 0, 0, 0) == 0, "prctl PR_GET_NAME");
    require(strcmp(name, "compat_abi") == 0, "prctl PR_GET_NAME value");

    /* Working directory */
    char cwd[128];
    require(getcwd(cwd, sizeof(cwd)) != 0, "getcwd");
    require(cwd[0] == '/', "getcwd absolute");

    /* Select and pselect6 */
    int pipefd[2] = { -1, -1 };
    require(pipe(pipefd) == 0, "pipe");

    fd_set rfds;
    zero_fdset(&rfds);
    set_fd(&rfds, pipefd[0]);
    struct timeval tv = { 0, 0 };
    require(select(pipefd[0] + 1, &rfds, 0, 0, &tv) == 0, "select timeout");

    int pid = fork();
    if (pid == 0) {
        nap_ms(50);
        write(pipefd[1], "x", 1);
        exit(0);
    }
    zero_fdset(&rfds);
    set_fd(&rfds, pipefd[0]);
    tv.tv_sec = 1;
    tv.tv_usec = 0;
    require(select(pipefd[0] + 1, &rfds, 0, 0, &tv) > 0 && (rfds.fds_bits[pipefd[0] / 64] & (1ULL << (pipefd[0] % 64))), "select pipe readiness");
    char drain[8];
    read(pipefd[0], drain, sizeof(drain));
    int st = 0;
    wait4(pid, &st, 0, 0);

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = ignore_handler;
    require(sigaction(SIGUSR1, &sa, 0) == 0, "sigaction");
    pid = fork();
    if (pid == 0) {
        nap_ms(50);
        kill(getppid(), SIGUSR1);
        exit(0);
    }
    zero_fdset(&rfds);
    set_fd(&rfds, pipefd[0]);
    struct timespec ts = { 1, 0 };
    require(pselect6(pipefd[0] + 1, &rfds, 0, 0, &ts, 0) == -EINTR, "pselect6 interruption");
    wait4(pid, &st, 0, 0);

    /* Epoll */
    int epfd = epoll_create1(0);
    require(epfd >= 0, "epoll_create1");
    struct epoll_event ev;
    ev.events = 1;
    ev.data = 0x1234;
    require(epoll_ctl(epfd, EPOLL_CTL_ADD, pipefd[0], &ev) == 0, "epoll_ctl add");
    struct epoll_event events[4];
    memset(events, 0, sizeof(events));
    require(epoll_wait(epfd, events, 4, 0) == 0, "epoll_wait timeout");

    ev.events = 1;
    ev.data = 0x4321;
    require(epoll_ctl(epfd, EPOLL_CTL_DEL, pipefd[0], 0) == 0, "epoll_ctl del");
    require(epoll_ctl(epfd, EPOLL_CTL_ADD, pipefd[0], &ev) == 0, "epoll_ctl add again");
    pid = fork();
    if (pid == 0) {
        nap_ms(50);
        write(pipefd[1], "y", 1);
        exit(0);
    }
    require(epoll_wait(epfd, events, 4, 1000) > 0, "epoll_wait pipe readiness");
    wait4(pid, &st, 0, 0);

    int sv_epoll[2] = { -1, -1 };
    require(socketpair(AF_UNIX, SOCK_STREAM, 0, sv_epoll) == 0, "socketpair epoll");
    ev.events = 1;
    ev.data = 0x7777;
    require(epoll_ctl(epfd, EPOLL_CTL_ADD, sv_epoll[1], &ev) == 0, "epoll_ctl socket add");
    require(write(sv_epoll[0], "z", 1) == 1, "socketpair write");
    require(epoll_wait(epfd, events, 4, 1000) > 0, "epoll_wait socket readiness");

    /* Socketpair + sendmsg/recvmsg */
    int sv[2] = { -1, -1 };
    require(socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0, "socketpair msg");
    char payload[] = "compat abi";
    struct iovec tx_iov = { payload, strlen(payload) };
    struct msghdr tx = { 0, 0, 0, &tx_iov, 1, 0, 0, 0 };
    require(sendmsg(sv[0], &tx, 0) == (ssize_t)strlen(payload), "sendmsg");

    char rxbuf[32];
    memset(rxbuf, 0, sizeof(rxbuf));
    struct iovec rx_iov = { rxbuf, sizeof(rxbuf) };
    struct msghdr rx = { 0, 0, 0, &rx_iov, 1, 0, 0, 0 };
    require(recvmsg(sv[1], &rx, 0) == (ssize_t)strlen(payload), "recvmsg");
    require(memcmp(rxbuf, payload, strlen(payload)) == 0, "socketpair payload");

    /* Memory management */
    void *mem = mmap(0, 8192, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    require(mem != MAP_FAILED, "mmap");
    ((volatile uint32_t *)mem)[0] = 0xdeadbeef;
    require(((volatile uint32_t *)mem)[0] == 0xdeadbeef, "mmap write/read");
    require(madvise(mem, 4096, MADV_DONTNEED) == 0, "madvise");
    void *remapped = mremap(mem, 8192, 4096, MREMAP_MAYMOVE, 0);
    require(remapped != MAP_FAILED, "mremap");
    require(munmap(remapped, 4096) == 0, "munmap after mremap");

    close(pipefd[0]);
    close(pipefd[1]);
    close(epfd);
    close(sv_epoll[0]);
    close(sv_epoll[1]);
    close(sv[0]);
    close(sv[1]);

    printf("COMPAT_ABI: all tests passed\n");
    return 0;
}

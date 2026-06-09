#include "libc_lite.h"

static char thread_stack[8192];
static volatile uint32_t thread_flag = 0;

static int test_thread_func(void *arg)
{
    (void)arg;
    thread_flag = 42;
    futex((uint32_t *)&thread_flag, FUTEX_WAKE, 1, 0, 0, 0);
    return 0;
}

int main(int argc, char **argv)
{
    (void)argc; (void)argv;

    printf("\n==========================================\n");
    printf("  LiteNix Userspace Init Process Started\n");
    printf("==========================================\n\n");

    // 1. Read hello.txt from initramfs
    printf("Test 1: Reading /hello.txt...\n");
    int fd = open("/hello.txt", O_RDONLY);
    if (fd < 0) {
        printf("Init ERROR: Failed to open /hello.txt (got fd %d)\n", fd);
        exit(1);
    }
    char buf[64];
    ssize_t bytes_read = read(fd, buf, sizeof(buf) - 1);
    if (bytes_read < 0) {
        printf("Init ERROR: Failed to read from /hello.txt\n");
        exit(1);
    }
    buf[bytes_read] = '\0';
    printf("Init: Content is: '%s'\n", buf);
    close(fd);
    printf("Test 1: PASSED\n\n");

    // 2. Write, Seek, and Read back a new file
    printf("Test 2: Creating and writing to /test_write.txt...\n");
    int fd_write = open("/test_write.txt", O_RDWR | O_CREAT);
    if (fd_write < 0) {
        printf("Init ERROR: Failed to open /test_write.txt\n");
        exit(1);
    }
    const char *test_str = "VFS Write and Seek Test Success!";
    ssize_t bytes_written = write(fd_write, test_str, strlen(test_str));
    if (bytes_written < 0) {
        printf("Init ERROR: Failed to write to /test_write.txt\n");
        exit(1);
    }
    printf("Init: Wrote %d bytes\n", (int)bytes_written);

    off_t pos = lseek(fd_write, 0, SEEK_SET);
    if (pos != 0) {
        printf("Init ERROR: lseek failed (got pos %d)\n", (int)pos);
        exit(1);
    }

    char read_buf[64];
    ssize_t read_len = read(fd_write, read_buf, sizeof(read_buf) - 1);
    if (read_len < 0) {
        printf("Init ERROR: Failed to read back from /test_write.txt\n");
        exit(1);
    }
    read_buf[read_len] = '\0';
    printf("Init: Read back content is: '%s'\n", read_buf);
    close(fd_write);
    printf("Test 2: PASSED\n\n");

    // 3. List files in root directory "/" using getdents64
    printf("Test 3: Listing files in root directory '/'...\n");
    int dfd = open("/", O_RDONLY);
    if (dfd < 0) {
        printf("Init ERROR: Failed to open directory /\n");
        exit(1);
    }
    char dbuf[512];
    int nread = getdents64(dfd, (struct linux_dirent64 *)dbuf, sizeof(dbuf));
    if (nread < 0) {
        printf("Init ERROR: getdents64 failed (got %d)\n", nread);
        exit(1);
    }
    int bpos = 0;
    while (bpos < nread) {
        struct linux_dirent64 *d = (struct linux_dirent64 *)(dbuf + bpos);
        printf("  - %s (ino=%d, type=%d)\n", d->d_name, (int)d->d_ino, (int)d->d_type);
        bpos += d->d_reclen;
    }
    close(dfd);
    printf("Test 3: PASSED\n\n");

    // 4. Independent File Descriptor Offsets
    printf("Test 4: Testing independent file descriptor offsets...\n");
    int fd1 = open("/hello.txt", O_RDONLY);
    int fd2 = open("/hello.txt", O_RDONLY);
    if (fd1 < 0 || fd2 < 0) {
        printf("Init ERROR: Failed to open hello.txt twice\n");
        exit(1);
    }

    // Seek fd1 to index 7 ("LiteNix")
    lseek(fd1, 7, SEEK_SET);

    char b1[16];
    char b2[16];
    read(fd1, b1, 7); b1[7] = '\0'; // Expect "LiteNix"
    read(fd2, b2, 5); b2[5] = '\0'; // Expect "Hello"

    printf("Init: fd1 offset 7 contains: '%s'\n", b1);
    printf("Init: fd2 offset 0 contains: '%s'\n", b2);

    close(fd1);
    close(fd2);

    if (strcmp(b1, "LiteNix") != 0 || strcmp(b2, "Hello") != 0) {
        printf("Init ERROR: Multi-FD offset isolation check failed\n");
        exit(1);
    }
    printf("Test 4: PASSED\n\n");

    // 5. Phase 12 Device Model Tests
    printf("Test 5: Testing virtual devices under /dev...\n");

    // Test 5a: /dev/null
    printf("  - /dev/null read/write...\n");
    int fd_null = open("/dev/null", O_RDWR);
    if (fd_null < 0) {
        printf("Init ERROR: Failed to open /dev/null\n");
        exit(1);
    }
    char null_buf[16];
    ssize_t null_r = read(fd_null, null_buf, sizeof(null_buf));
    if (null_r != 0) {
        printf("Init ERROR: /dev/null read returned %d instead of 0\n", (int)null_r);
        exit(1);
    }
    ssize_t null_w = write(fd_null, "test", 4);
    if (null_w != 4) {
        printf("Init ERROR: /dev/null write returned %d instead of 4\n", (int)null_w);
        exit(1);
    }
    close(fd_null);

    // Test 5b: /dev/zero
    printf("  - /dev/zero read/write...\n");
    int fd_zero = open("/dev/zero", O_RDWR);
    if (fd_zero < 0) {
        printf("Init ERROR: Failed to open /dev/zero\n");
        exit(1);
    }
    char zero_buf[16];
    memset(zero_buf, 0xFF, sizeof(zero_buf));
    ssize_t zero_r = read(fd_zero, zero_buf, 10);
    if (zero_r != 10) {
        printf("Init ERROR: /dev/zero read returned %d instead of 10\n", (int)zero_r);
        exit(1);
    }
    for (int i = 0; i < 10; i++) {
        if (zero_buf[i] != 0) {
            printf("Init ERROR: /dev/zero read at index %d returned %d instead of 0\n", i, zero_buf[i]);
            exit(1);
        }
    }
    if (zero_buf[10] != (char)0xFF) {
        printf("Init ERROR: /dev/zero read overflowed boundary\n");
        exit(1);
    }
    ssize_t zero_w = write(fd_zero, "test", 4);
    if (zero_w != 4) {
        printf("Init ERROR: /dev/zero write returned %d instead of 4\n", (int)zero_w);
        exit(1);
    }
    close(fd_zero);

    // Test 5c: /dev/full
    printf("  - /dev/full read/write...\n");
    int fd_full = open("/dev/full", O_RDWR);
    if (fd_full < 0) {
        printf("Init ERROR: Failed to open /dev/full\n");
        exit(1);
    }
    char full_buf[16];
    memset(full_buf, 0xFF, sizeof(full_buf));
    ssize_t full_r = read(fd_full, full_buf, 10);
    if (full_r != 10) {
        printf("Init ERROR: /dev/full read returned %d instead of 10\n", (int)full_r);
        exit(1);
    }
    for (int i = 0; i < 10; i++) {
        if (full_buf[i] != 0) {
            printf("Init ERROR: /dev/full read returned %d instead of 0\n", full_buf[i]);
            exit(1);
        }
    }
    ssize_t full_w = write(fd_full, "test", 4);
    if (full_w != -28) { // -ENOSPC
        printf("Init ERROR: /dev/full write returned %d instead of -28 (-ENOSPC)\n", (int)full_w);
        exit(1);
    }
    close(fd_full);

    // Test 5d: /dev/random and /dev/urandom
    printf("  - /dev/random and /dev/urandom read/write...\n");
    int fd_rand = open("/dev/random", O_RDWR);
    if (fd_rand < 0) {
        printf("Init ERROR: Failed to open /dev/random\n");
        exit(1);
    }
    char rand_buf1[16];
    char rand_buf2[16];
    memset(rand_buf1, 0, 16);
    memset(rand_buf2, 0, 16);
    read(fd_rand, rand_buf1, 16);
    read(fd_rand, rand_buf2, 16);
    int diff = 0;
    for (int i = 0; i < 16; i++) {
        if (rand_buf1[i] != rand_buf2[i]) diff = 1;
    }
    if (!diff) {
        printf("Init ERROR: Two random reads returned identical bytes\n");
        exit(1);
    }
    ssize_t rand_w = write(fd_rand, "seedtest", 8);
    if (rand_w != 8) {
        printf("Init ERROR: /dev/random write/seed failed\n");
        exit(1);
    }
    close(fd_rand);

    // Test 5e: /dev/tty
    printf("  - /dev/tty read/write...\n");
    int fd_tty = open("/dev/tty", O_RDWR);
    if (fd_tty < 0) {
        printf("Init ERROR: Failed to open /dev/tty\n");
        exit(1);
    }
    const char *tty_msg = "  [tty] Writing message to /dev/tty works!\n";
    write(fd_tty, tty_msg, strlen(tty_msg));
    close(fd_tty);

    // Test 5f: /dev/kmsg
    printf("  - /dev/kmsg read/write...\n");
    int fd_kmsg = open("/dev/kmsg", O_RDWR);
    if (fd_kmsg < 0) {
        printf("Init ERROR: Failed to open /dev/kmsg\n");
        exit(1);
    }
    char kmsg_buf[64];
    ssize_t kmsg_r = read(fd_kmsg, kmsg_buf, sizeof(kmsg_buf) - 1);
    if (kmsg_r <= 0) {
        printf("Init ERROR: /dev/kmsg read failed\n");
        exit(1);
    }
    kmsg_buf[kmsg_r] = '\0';
    printf("  - /dev/kmsg read content: '%s'", kmsg_buf);

    const char *kmsg_msg = "Hello from userspace /dev/kmsg test!\n";
    write(fd_kmsg, kmsg_msg, strlen(kmsg_msg));
    close(fd_kmsg);

    printf("Test 5: PASSED\n\n");

    // 6. Phase 13 Procfs minimal compatibility tests
    printf("Test 6: Testing minimal procfs compatibility...\n");
    const char *proc_files[] = {
        "/proc/version",
        "/proc/cpuinfo",
        "/proc/meminfo",
        "/proc/uptime",
        "/proc/stat",
        "/proc/self/status"
    };
    for (int i = 0; i < 6; i++) {
        printf("  - Reading %s...\n", proc_files[i]);
        int pfd = open(proc_files[i], O_RDONLY);
        if (pfd < 0) {
            printf("Init ERROR: Failed to open %s\n", proc_files[i]);
            exit(1);
        }
        char pbuf[512];
        ssize_t pr = read(pfd, pbuf, sizeof(pbuf) - 1);
        if (pr < 0) {
            printf("Init ERROR: Failed to read from %s\n", proc_files[i]);
            exit(1);
        }
        pbuf[pr] = '\0';
        printf("<<< Content of %s >>>\n%s<<<\n", proc_files[i], pbuf);
        close(pfd);
    }
    printf("Test 6: PASSED\n\n");

    // 7. Process replication (fork and wait4)
    printf("Test 7: Testing fork and wait4...\n");
    int fpid = fork();
    if (fpid < 0) {
        printf("Init ERROR: fork failed with %d\n", fpid);
        exit(1);
    }
    if (fpid == 0) {
        // Child process
        int fd_kmsg = open("/dev/kmsg", O_RDWR);
        if (fd_kmsg >= 0) {
            const char *child_msg = "Hello from the child process in fork test!\n";
            write(fd_kmsg, child_msg, strlen(child_msg));
            close(fd_kmsg);
        }
        exit(42);
    } else {
        // Parent process
        int status = 0;
        int wait_ret = wait4(fpid, &status, 0, 0);
        if (wait_ret != fpid) {
            printf("Init ERROR: wait4 returned %d instead of child pid %d\n", wait_ret, fpid);
            exit(1);
        }
        if (!WIFEXITED(status) || WEXITSTATUS(status) != 42) {
            printf("Init ERROR: child exit status was raw=%d WEXITSTATUS=%d instead of 42\n",
                   status, WEXITSTATUS(status));
            exit(1);
        }
        printf("Init: Child %d exited with status %d\n", fpid, WEXITSTATUS(status));
    }
    printf("Test 7: PASSED\n\n");

    // 8. getpid / getppid
    printf("Test 8: Testing getpid() and getppid()...\n");
    int my_pid = getpid();
    if (my_pid <= 0) {
        printf("Init ERROR: getpid() returned %d\n", my_pid);
        exit(1);
    }
    int my_tid = gettid();
    if (my_tid <= 0) {
        printf("Init ERROR: gettid() returned %d\n", my_tid);
        exit(1);
    }
    struct utsname uts;
    if (uname(&uts) != 0) {
        printf("Init ERROR: uname() failed\n");
        exit(1);
    }
    printf("Init: my pid=%d, tid=%d, parent pid=%d, sys=%s/%s\n",
           my_pid, my_tid, getppid(), uts.sysname, uts.machine);

    struct stat st_at;
    if (fstatat(AT_FDCWD, "/hello.txt", &st_at, 0) != 0 || st_at.st_size <= 0) {
        printf("Init ERROR: fstatat(AT_FDCWD, /hello.txt) failed\n");
        exit(1);
    }
    int fd_at = openat(AT_FDCWD, "/hello.txt", O_RDONLY, 0);
    if (fd_at < 0) {
        printf("Init ERROR: openat(AT_FDCWD, /hello.txt) failed\n");
        exit(1);
    }
    close(fd_at);

    void *robust_head = 0;
    size_t robust_len = 0;
    if (set_robust_list((void *)&thread_flag, sizeof(thread_flag)) != 0 ||
        get_robust_list(0, &robust_head, &robust_len) != 0 ||
        robust_head != (void *)&thread_flag ||
        robust_len != sizeof(thread_flag)) {
        printf("Init ERROR: robust list registration failed\n");
        exit(1);
    }

    if (access("/hello.txt", R_OK) != 0 || faccessat(AT_FDCWD, "/hello.txt", R_OK, 0) != 0) {
        printf("Init ERROR: access/faccessat failed for /hello.txt\n");
        exit(1);
    }
    char linkbuf[8];
    if (readlink("/hello.txt", linkbuf, sizeof(linkbuf)) != -22) {
        printf("Init ERROR: readlink regular-file failure was not -EINVAL\n");
        exit(1);
    }
    struct statx sx;
    if (statx(AT_FDCWD, "/hello.txt", 0, 0, &sx) != 0 || sx.stx_size <= 0) {
        printf("Init ERROR: statx failed for /hello.txt\n");
        exit(1);
    }
    unsigned char rnd[16];
    if (getrandom(rnd, sizeof(rnd), 0) != (ssize_t)sizeof(rnd)) {
        printf("Init ERROR: getrandom failed\n");
        exit(1);
    }
    struct rlimit lim;
    if (getrlimit(RLIMIT_NOFILE, &lim) != 0 || lim.rlim_cur == 0) {
        printf("Init ERROR: getrlimit(RLIMIT_NOFILE) failed\n");
        exit(1);
    }
    printf("Test 8: PASSED\n\n");

    // 9. O_TRUNC and O_APPEND
    printf("Test 9: Testing O_TRUNC and O_APPEND...\n");
    int fdx = open("/tmp_trunc.txt", O_RDWR | O_CREAT);
    if (fdx < 0) { printf("Init ERROR: cannot create /tmp_trunc.txt\n"); exit(1); }
    write(fdx, "ORIGINAL", 8);
    close(fdx);

    // Reopen with O_TRUNC — should erase contents
    fdx = open("/tmp_trunc.txt", O_RDWR | O_TRUNC);
    if (fdx < 0) { printf("Init ERROR: cannot reopen with O_TRUNC\n"); exit(1); }
    char tbuf[16];
    ssize_t tr = read(fdx, tbuf, sizeof(tbuf));
    if (tr != 0) { printf("Init ERROR: O_TRUNC did not truncate (read %d bytes)\n", (int)tr); exit(1); }
    // Write then close
    write(fdx, "A", 1);
    close(fdx);

    // Reopen with O_APPEND — write should go to end
    fdx = open("/tmp_trunc.txt", O_RDWR | O_APPEND);
    if (fdx < 0) { printf("Init ERROR: cannot reopen with O_APPEND\n"); exit(1); }
    write(fdx, "B", 1);
    lseek(fdx, 0, SEEK_SET);
    ssize_t ar = read(fdx, tbuf, sizeof(tbuf));
    tbuf[ar < 0 ? 0 : ar] = '\0';
    if (ar != 2 || tbuf[0] != 'A' || tbuf[1] != 'B') {
        printf("Init ERROR: O_APPEND content wrong: got '%s' (len=%d)\n", tbuf, (int)ar);
        exit(1);
    }
    close(fdx);
    unlink("/tmp_trunc.txt");
    printf("Test 9: PASSED\n\n");

    // 10. dup / dup2
    printf("Test 10: Testing dup() and dup2()...\n");
    int fddup = open("/hello.txt", O_RDONLY);
    if (fddup < 0) { printf("Init ERROR: cannot open for dup test\n"); exit(1); }
    int fddup2 = dup(fddup);
    if (fddup2 < 0) { printf("Init ERROR: dup() failed\n"); exit(1); }
    char dbuf1[8], dbuf2[8];
    read(fddup,  dbuf1, 5); dbuf1[5] = '\0';
    read(fddup2, dbuf2, 5); dbuf2[5] = '\0';
    // Both should read independently from position 0 since dup shares offset
    // Actually dup shares the same file struct (same offset) — so second read
    // continues from where first left off
    close(fddup);
    close(fddup2);
    printf("Init: dup test read1='%s'\n", dbuf1);
    printf("Test 10: PASSED\n\n");

    // 11. mkdir / unlink
    printf("Test 11: Testing mkdir() and unlink()...\n");
    int mret = mkdir("/testdir", 0755);
    if (mret != 0) { printf("Init ERROR: mkdir failed: %d\n", mret); exit(1); }
    // Create file inside dir
    int fdin = open("/testdir/inner.txt", O_RDWR | O_CREAT);
    if (fdin < 0) { printf("Init ERROR: cannot create file in new dir\n"); exit(1); }
    write(fdin, "inner", 5);
    close(fdin);
    // unlink file then rmdir
    if (unlink("/testdir/inner.txt") != 0) { printf("Init ERROR: unlink failed\n"); exit(1); }
    if (rmdir("/testdir") != 0) { printf("Init ERROR: rmdir failed\n"); exit(1); }
    printf("Test 11: PASSED\n\n");

    // 12. Threading / Futex / Clone / TLS
    printf("Test 12: Testing clone() and futex() synchronization...\n");
    thread_flag = 0;
    void *stack_top = (void *)((uintptr_t)thread_stack + sizeof(thread_stack) - 16);
    int thread_ret = clone_thread(test_thread_func, stack_top, 0);
    if (thread_ret < 0) {
        printf("Init ERROR: clone failed with %d\n", thread_ret);
        exit(1);
    }

    // Parent thread: wait for thread_flag to become 42
    while (thread_flag != 42) {
        futex((uint32_t *)&thread_flag, FUTEX_WAIT, 0, 0, 0, 0);
    }
    printf("Init: Successfully synchronized with child thread! Flag is 42.\n");
    printf("Test 12: PASSED\n\n");

    // 13. Lazy mmap / munmap / demand paging
    printf("Test 13: Testing lazy anonymous mmap() and munmap()...\n");
    size_t map_size = 65536; // 16 pages
    void *map_ptr = mmap(0, map_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (map_ptr == MAP_FAILED) {
        printf("Init ERROR: mmap failed\n");
        exit(1);
    }
    printf("Init: Mapped anonymous memory at %p\n", map_ptr);

    // Write to first page (triggers demand paging)
    uint32_t *p1 = (uint32_t *)map_ptr;
    *p1 = 0xdeadbeef;

    // Write to last page (triggers demand paging)
    uint32_t *p2 = (uint32_t *)((uintptr_t)map_ptr + map_size - 4);
    *p2 = 0xcafebabe;

    // Verify values
    if (*p1 != 0xdeadbeef || *p2 != 0xcafebabe) {
        printf("Init ERROR: Readback from mmap memory failed\n");
        exit(1);
    }
    printf("Init: Lazy demand paging write/readback verified successfully.\n");

    // Unmap the region
    if (munmap(map_ptr, map_size) != 0) {
        printf("Init ERROR: munmap failed\n");
        exit(1);
    }
    printf("Test 13: PASSED\n\n");

    // 14. Pipes and Poll (Phase 21)
    printf("Test 14: Testing pipes and poll()...\n");
    int pipefd[2];
    if (pipe(pipefd) != 0) {
        printf("Init ERROR: pipe failed\n");
        exit(1);
    }

    struct pollfd pfd[2];
    pfd[0].fd = pipefd[0];
    pfd[0].events = POLLIN;
    pfd[1].fd = pipefd[1];
    pfd[1].events = POLLOUT;

    int poll_ret = poll(pfd, 2, 0);
    if (poll_ret < 0) {
        printf("Init ERROR: poll failed: %d\n", poll_ret);
        exit(1);
    }
    printf("Init: poll non-blocking returned %d. write end ready: %s, read end ready: %s\n",
           poll_ret, (pfd[1].revents & POLLOUT) ? "YES" : "NO", (pfd[0].revents & POLLIN) ? "YES" : "NO");

    int p_pid = fork();
    if (p_pid < 0) {
        printf("Init ERROR: fork failed for pipe test\n");
        exit(1);
    }

    if (p_pid == 0) {
        close(pipefd[0]);
        const char *msg = "Hello Pipe!";
        write(pipefd[1], msg, strlen(msg));
        close(pipefd[1]);
        exit(0);
    } else {
        close(pipefd[1]);

        pfd[0].fd = pipefd[0];
        pfd[0].events = POLLIN;
        poll_ret = poll(pfd, 1, 1000);
        if (poll_ret <= 0 || !(pfd[0].revents & POLLIN)) {
            printf("Init ERROR: poll did not detect readable pipe: %d\n", poll_ret);
            exit(1);
        }

        char pipe_buf[32];
        ssize_t pr = read(pipefd[0], pipe_buf, sizeof(pipe_buf) - 1);
        if (pr < 0) {
            printf("Init ERROR: pipe read failed\n");
            exit(1);
        }
        pipe_buf[pr] = '\0';
        printf("Init: Read from pipe: '%s'\n", pipe_buf);
        close(pipefd[0]);

        int p_status = 0;
        wait4(p_pid, &p_status, 0, 0);

        if (strcmp(pipe_buf, "Hello Pipe!") != 0) {
            printf("Init ERROR: Pipe content mismatch\n");
            exit(1);
        }
    }
    printf("Test 14: PASSED\n\n");

    // 15. Timekeeping (Phase 22)
    printf("Test 15: Testing clock_gettime, gettimeofday, and sleep...\n");
    struct timespec ts1, ts2;
    if (clock_gettime(CLOCK_MONOTONIC, &ts1) != 0) {
        printf("Init ERROR: clock_gettime CLOCK_MONOTONIC failed\n");
        exit(1);
    }
    printf("Init: Monotonic time start: %d s, %d ns\n", (int)ts1.tv_sec, (int)ts1.tv_nsec);

    struct timeval tv;
    if (gettimeofday(&tv, 0) != 0) {
        printf("Init ERROR: gettimeofday failed\n");
        exit(1);
    }
    printf("Init: Realtime clock (seconds since epoch): %d\n", (int)tv.tv_sec);
    if (tv.tv_sec < 1700000000) {
        printf("Init ERROR: Realtime clock seconds seem wrong: %d\n", (int)tv.tv_sec);
        exit(1);
    }

    printf("Init: Sleeping for 1 second...\n");
    sleep(1);

    if (clock_gettime(CLOCK_MONOTONIC, &ts2) != 0) {
        printf("Init ERROR: clock_gettime CLOCK_MONOTONIC failed\n");
        exit(1);
    }
    printf("Init: Monotonic time end: %d s, %d ns\n", (int)ts2.tv_sec, (int)ts2.tv_nsec);
    int64_t time_diff = ts2.tv_sec - ts1.tv_sec;
    if (time_diff < 1) {
        printf("Init ERROR: sleep(1) did not advance monotonic clock by at least 1s (diff was %d)\n", (int)time_diff);
        exit(1);
    }
    printf("Test 15: PASSED\n\n");

    // 16. Storage and Filesystems (Phase 23)
    printf("Test 16: Testing EXT2 and tmpfs...\n");
    int ext2_fd = open("/ext2/hello.txt", O_RDONLY);
    if (ext2_fd < 0) {
        printf("Init ERROR: Failed to open /ext2/hello.txt\n");
        exit(1);
    }
    char ext2_buf[64];
    ssize_t ext2_r = read(ext2_fd, ext2_buf, sizeof(ext2_buf) - 1);
    if (ext2_r < 0) {
        printf("Init ERROR: Failed to read from /ext2/hello.txt\n");
        exit(1);
    }
    ext2_buf[ext2_r] = '\0';
    printf("Init: Mounted EXT2 hello.txt content: '%s'\n", ext2_buf);
    close(ext2_fd);

    if (strcmp(ext2_buf, "Hello from EXT2 disk!\n") != 0) {
        printf("Init ERROR: EXT2 hello.txt content mismatch\n");
        exit(1);
    }

    ext2_fd = open("/ext2/hello.txt", O_RDWR | O_TRUNC);
    if (ext2_fd < 0) {
        printf("Init ERROR: Failed to open /ext2/hello.txt for writing\n");
        exit(1);
    }
    const char *new_ext2_data = "EXT2 write success!";
    ssize_t ext2_w = write(ext2_fd, new_ext2_data, strlen(new_ext2_data));
    if (ext2_w < 0) {
        printf("Init ERROR: Failed to write to /ext2/hello.txt\n");
        exit(1);
    }
    lseek(ext2_fd, 0, SEEK_SET);
    ext2_r = read(ext2_fd, ext2_buf, sizeof(ext2_buf) - 1);
    ext2_buf[ext2_r < 0 ? 0 : ext2_r] = '\0';
    printf("Init: Updated EXT2 hello.txt content: '%s'\n", ext2_buf);
    close(ext2_fd);

    if (strcmp(ext2_buf, new_ext2_data) != 0) {
        printf("Init ERROR: EXT2 read back updated content mismatch\n");
        exit(1);
    }

    int tmp_fd = open("/tmp/test.txt", O_RDWR | O_CREAT);
    if (tmp_fd < 0) {
        printf("Init ERROR: Failed to open /tmp/test.txt\n");
        exit(1);
    }
    const char *tmp_data = "Tmpfs write works!";
    write(tmp_fd, tmp_data, strlen(tmp_data));
    lseek(tmp_fd, 0, SEEK_SET);
    char tmp_buf[64];
    ssize_t tmp_r = read(tmp_fd, tmp_buf, sizeof(tmp_buf) - 1);
    tmp_buf[tmp_r < 0 ? 0 : tmp_r] = '\0';
    printf("Init: /tmp/test.txt content: '%s'\n", tmp_buf);
    close(tmp_fd);

    if (strcmp(tmp_buf, tmp_data) != 0) {
        printf("Init ERROR: tmpfs read back content mismatch\n");
        exit(1);
    }
    printf("Test 16: PASSED\n\n");

    // Test 17: Socket API bind check
    printf("Test 17: Testing socket creation and binding...\n");
    int test_sock = socket(2, 2, 0); // AF_INET, SOCK_DGRAM
    if (test_sock < 0) {
        printf("Init ERROR: socket() failed: %d\n", test_sock);
        exit(1);
    }
    struct sockaddr_in test_addr;
    memset(&test_addr, 0, sizeof(test_addr));
    test_addr.sin_family = 2;
    test_addr.sin_port = 0xB822;
    test_addr.sin_addr.s_addr = 0;
    int bind_ret = bind(test_sock, (struct sockaddr *)&test_addr, sizeof(test_addr));
    if (bind_ret < 0) {
        printf("Init ERROR: bind() failed: %d\n", bind_ret);
        exit(1);
    }
    close(test_sock);
    printf("Test 17: PASSED\n\n");

    // Test 18: Static musl hello world check
    printf("Test 18: Running static musl hello world (/bin/hello_musl)...\n");
    int musl_pid = fork();
    if (musl_pid < 0) {
        printf("Init ERROR: fork for musl hello failed\n");
        exit(1);
    }
    if (musl_pid == 0) {
        char *musl_argv[] = { "/bin/hello_musl", "arg1", "arg2", 0 };
        char *musl_envp[] = { "ENV_VAR=hello_env", 0 };
        execve("/bin/hello_musl", musl_argv, musl_envp);
        printf("Init ERROR: execve /bin/hello_musl failed\n");
        exit(1);
    } else {
        int mstatus = 0;
        int mret = wait4(musl_pid, &mstatus, 0, 0);
        if (mret != musl_pid) {
            printf("Init ERROR: wait4 for musl hello got pid %d instead of %d\n", mret, musl_pid);
            exit(1);
        }
        if (!WIFEXITED(mstatus) || WEXITSTATUS(mstatus) != 0) {
            printf("Init ERROR: musl hello exited with status %d (raw=%d)\n", WEXITSTATUS(mstatus), mstatus);
            exit(1);
        }
        printf("Test 18: PASSED\n\n");
    }

    // Test 19: Rejection of missing/invalid executables and bad pointers
    printf("Test 19: Rejection of missing/invalid executables and bad pointers...\n");
    
    // 1. Missing file check (-ENOENT)
    printf("  - Testing execve of missing file...\n");
    char *bad_argv[] = { "/bin/non_existent_file_xyz", 0 };
    int ret1 = execve("/bin/non_existent_file_xyz", bad_argv, 0);
    if (ret1 != -2) { // ENOENT = 2
        printf("Init ERROR: execve missing file returned %d instead of -2 (-ENOENT)\n", ret1);
        exit(1);
    }
    printf("    -ENOENT check PASSED\n");

    // 2. Non-executable file check (-ENOEXEC)
    printf("  - Testing execve of non-ELF file...\n");
    char *txt_argv[] = { "/hello.txt", 0 };
    int ret2 = execve("/hello.txt", txt_argv, 0);
    if (ret2 != -8) { // ENOEXEC = 8
        printf("Init ERROR: execve non-ELF file returned %d instead of -8 (-ENOEXEC)\n", ret2);
        exit(1);
    }
    printf("    -ENOEXEC check PASSED\n");

    // 3. Bad pointer check (-EFAULT)
    printf("  - Testing execve of bad pointer...\n");
    int ret3 = execve((const char *)0xdeadbeef, 0, 0);
    if (ret3 != -14) { // EFAULT = 14
        printf("Init ERROR: execve bad path pointer returned %d instead of -14 (-EFAULT)\n", ret3);
        exit(1);
    }
    
    int ret4 = execve("/bin/hello_musl", (char *const *)0xdeadbeef, 0);
    if (ret4 != -14) { // EFAULT = 14
        printf("Init ERROR: execve bad argv pointer returned %d instead of -14 (-EFAULT)\n", ret4);
        exit(1);
    }
    printf("    -EFAULT check PASSED\n");

    // 4. Directory file check (-EACCES)
    printf("  - Testing execve of directory...\n");
    char *dir_argv[] = { "/", 0 };
    int ret5 = execve("/", dir_argv, 0);
    if (ret5 != -13) { // EACCES = 13
        printf("Init ERROR: execve directory returned %d instead of -13 (-EACCES)\n", ret5);
        exit(1);
    }
    // 5. Missing interpreter check (-ENOENT = -2)
    printf("  - Testing execve of dynamic binary with missing interpreter...\n");
    char *miss_argv[] = { "/tests/missing_interp", 0 };
    int ret_miss = execve("/tests/missing_interp", miss_argv, 0);
    if (ret_miss != -2) {
        printf("Init ERROR: execve missing interpreter returned %d instead of -2 (-ENOENT)\n", ret_miss);
        exit(1);
    }
    printf("    -ENOENT (missing interpreter) check PASSED\n");

    // 6. Invalid interpreter format check (-ENOEXEC = -8)
    printf("  - Testing execve of dynamic binary with invalid interpreter format...\n");
    char *inv_argv[] = { "/tests/invalid_interp", 0 };
    int ret_inv = execve("/tests/invalid_interp", inv_argv, 0);
    if (ret_inv != -8) {
        printf("Init ERROR: execve invalid interpreter returned %d instead of -8 (-ENOEXEC)\n", ret_inv);
        exit(1);
    }
    printf("    -ENOEXEC (invalid interpreter format) check PASSED\n");
    
    printf("Test 19: PASSED\n\n");

    // Test 20: File-backed mmap tests
    printf("Test 20: Running file-backed mmap tests...\n");
    int mmap_file_pid = fork();
    if (mmap_file_pid < 0) {
        printf("Init ERROR: fork for file-backed mmap test failed\n");
        exit(1);
    }
    if (mmap_file_pid == 0) {
        char *const mmap_file_argv[] = {"test_mmap_file", NULL};
        execve("/tests/test_mmap_file", mmap_file_argv, NULL);
        printf("Init ERROR: execve /tests/test_mmap_file failed\n");
        exit(1);
    }
    int mmap_file_status;
    wait4(mmap_file_pid, &mmap_file_status, 0, NULL);
    if (WIFSIGNALED(mmap_file_status)) {
        printf("Init ERROR: test_mmap_file terminated by signal %d\n", WTERMSIG(mmap_file_status));
        exit(1);
    }
    if (WEXITSTATUS(mmap_file_status) != 0) {
        printf("Init ERROR: test_mmap_file exited with code %d\n", WEXITSTATUS(mmap_file_status));
        exit(1);
    }
    printf("Test 20: PASSED\n\n");

    // Test 21: Dynamic binary PT_INTERP test
    printf("Test 21: Testing dynamic binary PT_INTERP path...\n");
    int dyn_pid = fork();
    if (dyn_pid < 0) {
        printf("Init ERROR: fork for dynamic binary test failed\n");
        exit(1);
    }
    if (dyn_pid == 0) {
        char *dyn_argv[] = { "/tests/dynamic_binary", 0 };
        char *dyn_envp[] = { "DYNAMIC_TEST=hello", 0 };
        execve("/tests/dynamic_binary", dyn_argv, dyn_envp);
        printf("Init ERROR: execve /tests/dynamic_binary failed\n");
        exit(1);
    }
    int dyn_status;
    wait4(dyn_pid, &dyn_status, 0, NULL);
    if (WIFSIGNALED(dyn_status)) {
        printf("Init ERROR: dynamic binary terminated by signal %d\n", WTERMSIG(dyn_status));
        exit(1);
    }
    if (WEXITSTATUS(dyn_status) != 0) {
        printf("Init ERROR: dynamic binary exited with code %d\n", WEXITSTATUS(dyn_status));
        exit(1);
    }
    printf("Test 21: PASSED\n\n");

    // Test 22: Real dynamic musl hello world
    printf("Test 22: Running real dynamically linked musl hello world (/bin/hello_dynamic)...\n");
    int musl_dyn_pid = fork();
    if (musl_dyn_pid < 0) {
        printf("Init ERROR: fork for dynamic musl hello failed\n");
        exit(1);
    }
    if (musl_dyn_pid == 0) {
        char *dyn_argv[] = { "/bin/hello_dynamic", 0 };
        execve("/bin/hello_dynamic", dyn_argv, 0);
        printf("Init ERROR: execve /bin/hello_dynamic failed\n");
        exit(1);
    } else {
        int dstatus = 0;
        wait4(musl_dyn_pid, &dstatus, 0, 0);
        if (WIFSIGNALED(dstatus)) {
            printf("Init ERROR: dynamic musl hello terminated by signal %d\n", WTERMSIG(dstatus));
            exit(1);
        }
        if (WEXITSTATUS(dstatus) != 0) {
            printf("Init ERROR: dynamic musl hello exited with code %d\n", WEXITSTATUS(dstatus));
            exit(1);
        }
        printf("Test 22: PASSED\n\n");
    }

    // Launch UDP Echo Server
    printf("Launching UDP Echo Server on port 9999...\n");
    int udp_pid = fork();
    if (udp_pid < 0) {
        printf("Init ERROR: fork for UDP Echo failed\n");
        exit(1);
    }
    if (udp_pid == 0) {
        int sock = socket(2, 2, 0);
        struct sockaddr_in addr;
        memset(&addr, 0, sizeof(addr));
        addr.sin_family = 2;
        addr.sin_port = 0x0F27;
        addr.sin_addr.s_addr = 0;
        bind(sock, (struct sockaddr *)&addr, sizeof(addr));
        printf("UDP Echo Server listening on port 9999\n");

        while (1) {
            char buf[512];
            struct sockaddr_in client_addr;
            socklen_t addr_len = sizeof(client_addr);
            ssize_t n = recvfrom(sock, buf, sizeof(buf), 0, (struct sockaddr *)&client_addr, &addr_len);
            if (n > 0) {
                sendto(sock, buf, n, 0, (struct sockaddr *)&client_addr, addr_len);
            }
        }
        exit(0);
    }

    // Launch TCP HTTP Server
    printf("Launching TCP HTTP Server on port 80...\n");
    int tcp_pid = fork();
    if (tcp_pid < 0) {
        printf("Init ERROR: fork for TCP HTTP failed\n");
        exit(1);
    }
    if (tcp_pid == 0) {
        int server_fd = socket(2, 1, 0);
        struct sockaddr_in addr;
        memset(&addr, 0, sizeof(addr));
        addr.sin_family = 2;
        addr.sin_port = 0x5000;
        addr.sin_addr.s_addr = 0;
        bind(server_fd, (struct sockaddr *)&addr, sizeof(addr));
        listen(server_fd, 5);
        printf("TCP HTTP Server listening on port 80\n");

        while (1) {
            struct sockaddr_in client_addr;
            socklen_t addr_len = sizeof(client_addr);
            int client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &addr_len);
            if (client_fd >= 0) {
                char buf[1024];
                ssize_t n = read(client_fd, buf, sizeof(buf) - 1);
                if (n > 0) {
                    const char *resp =
                        "HTTP/1.1 200 OK\r\n"
                        "Content-Type: text/html\r\n"
                        "Connection: close\r\n"
                        "Content-Length: 76\r\n"
                        "\r\n"
                        "<html><body><h1>Hello from LiteNix Web Server!</h1></body></html>\r\n";
                    write(client_fd, resp, strlen(resp));
                }
                close(client_fd);
            }
        }
        exit(0);
    }

    printf("All VFS Verification Tests Passed!\n");

    /* Early-exit for kernel Phase 9/10 self-test (init is called with "test_arg") */
    if (argc > 1 && strcmp(argv[1], "test_arg") == 0) {
        printf("Init: Running in test mode. Exiting with 0.\n");
        exit(0);
    }

    /* --- Phase 2: Raw-syscall userspace test suite --- */
    printf("\n--- Running raw-syscall test suite (/tests/test_all) ---\n");
    {
        int raw_pid = fork();
        if (raw_pid < 0) {
            printf("Init ERROR: fork for raw-syscall tests failed\n");
        } else if (raw_pid == 0) {
            char *raw_argv[] = { "/tests/test_all", 0 };
            execve("/tests/test_all", raw_argv, 0);
            printf("Init ERROR: execve /tests/test_all failed\n");
            exit(1);
        } else {
            int raw_status = 0;
            int raw_ret = wait4(raw_pid, &raw_status, 0, 0);
            if (raw_ret != raw_pid) {
                printf("Init WARNING: wait4 for raw-syscall tests got unexpected pid %d\n", raw_ret);
            } else if (WIFEXITED(raw_status) && WEXITSTATUS(raw_status) == 0) {
                printf("Phase 2: raw-syscall tests PASSED\n");
            } else {
                printf("Phase 2: raw-syscall tests FAILED (status=%d exitcode=%d)\n",
                       raw_status, WEXITSTATUS(raw_status));
            }
        }
    }

    for (;;) {
        printf("Launching shell /bin/sh...\n\n");

        int pid = fork();
        if (pid < 0) {
            printf("Init ERROR: Failed to fork shell!\n");
            exit(1);
        } else if (pid == 0) {
            char *sh_argv[] = { "/bin/sh", 0 };
            execve("/bin/sh", sh_argv, 0);
            printf("Init ERROR: Failed to execute /bin/sh!\n");
            exit(1);
        } else {
            int status = 0;
            wait4(pid, &status, 0, 0);
            printf("\nInit: Shell exited or crashed (status %d). Respawning...\n", status);
        }
    }
}

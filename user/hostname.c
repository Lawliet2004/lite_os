/*
 * /bin/hostname — LiteNix hostname get/set.
 *
 * Usage:
 *   hostname             -> print current hostname
 *   hostname <name>      -> set the kernel hostname
 *
 * The kernel stores the hostname in /proc/sys/kernel/hostname, written via
 * a "set <value>" command on that file.
 */

#include "libc_lite.h"

int main(int argc, char **argv)
{
    if (argc == 1) {
        int fd = open("/proc/sys/kernel/hostname", O_RDONLY);
        if (fd < 0) {
            printf("hostname: cannot read /proc/sys/kernel/hostname\n");
            return 1;
        }
        char buf[128];
        ssize_t n = read(fd, buf, sizeof(buf) - 1);
        close(fd);
        if (n <= 0) {
            printf("hostname: empty hostname\n");
            return 0;
        }
        buf[n] = 0;
        /* Strip trailing newline */
        while (n > 0 && (buf[n - 1] == '\n' || buf[n - 1] == '\r')) {
            buf[--n] = 0;
        }
        printf("%s\n", buf);
        return 0;
    }

    if (argc > 2) {
        printf("Usage: hostname [<name>]\n");
        return 1;
    }

    int fd = open("/proc/sys/kernel/hostname", O_WRONLY);
    if (fd < 0) {
        printf("hostname: cannot open /proc/sys/kernel/hostname for writing\n");
        return 1;
    }
    const char *name = argv[1];
    size_t len = strlen(name);
    if (write(fd, name, len) != (ssize_t)len) {
        printf("hostname: write failed\n");
        close(fd);
        return 1;
    }
    close(fd);
    return 0;
}

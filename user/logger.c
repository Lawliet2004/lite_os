/*
 * /bin/logger -- write a message to the kernel log.
 *
 * Usage:
 *   logger <message...>            -> write "<args>\n" to /dev/kmsg
 *   logger -p <prio> <message...>  -> prefix a syslog-style priority
 *                                    (LiteNix ignores the priority for now
 *                                    but we accept the flag for
 *                                    compatibility with real logger(1).)
 *
 * Messages are written via the kernel's /dev/kmsg interface, which then
 * shows up in the kernel's ring buffer and is also persisted to
 * /var/log/kern.log by klogd if it is running.
 *
 * Examples:
 *   logger "boot completed"
 *   logger -p user.info "hello from init"
 */

#include "libc_lite.h"

int main(int argc, char **argv)
{
    if (argc < 2) {
        printf("Usage: logger [-p <prio>] <message>\n");
        return 1;
    }

    int start = 1;
    /* Accept -p <prio> for compatibility but do nothing with the value. */
    if (argc >= 3 && strcmp(argv[1], "-p") == 0) {
        start = 3;
    }

    /* Concatenate remaining args with single spaces. Bounded by the
     * kmsg write size of 255 bytes. */
    char buf[256];
    size_t pos = 0;
    for (int i = start; i < argc && pos + 1 < sizeof(buf); i++) {
        if (i > start && pos + 1 < sizeof(buf)) buf[pos++] = ' ';
        const char *s = argv[i];
        while (*s && pos + 1 < sizeof(buf)) buf[pos++] = *s++;
    }
    if (pos + 1 < sizeof(buf)) buf[pos++] = '\n';
    buf[pos] = '\0';

    int fd = open("/dev/kmsg", O_WRONLY);
    if (fd < 0) {
        printf("logger: cannot open /dev/kmsg\n");
        return 1;
    }
    ssize_t w = write(fd, buf, strlen(buf));
    close(fd);
    if (w < 0) {
        printf("logger: write failed\n");
        return 1;
    }
    return 0;
}

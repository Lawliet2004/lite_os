#include "libc_lite.h"

int main(int argc, char **argv)
{
    if (argc == 1) {
        // Read and print /proc/net/config
        int fd = open("/proc/net/config", O_RDONLY);
        if (fd < 0) {
            printf("ifconfig: cannot open /proc/net/config\n");
            return 1;
        }
        char buf[512];
        ssize_t n = read(fd, buf, sizeof(buf) - 1);
        if (n > 0) {
            buf[n] = '\0';
            printf("%s", buf);
        } else {
            printf("ifconfig: empty network config\n");
        }
        close(fd);
        return 0;
    }

    // Usage: ifconfig eth0 <ip> [netmask <mask>] [gw <gateway>]
    if (strcmp(argv[1], "eth0") != 0) {
        printf("Usage: ifconfig eth0 <ip> [netmask <mask>] [gw <gateway>]\n");
        return 1;
    }

    if (argc < 3) {
        printf("Usage: ifconfig eth0 <ip> [netmask <mask>] [gw <gateway>]\n");
        return 1;
    }

    int fd = open("/proc/net/config", O_WRONLY);
    if (fd < 0) {
        printf("ifconfig: cannot open /proc/net/config for writing\n");
        return 1;
    }

    char cmd[256];
    snprintf(cmd, sizeof(cmd), "ip %s\n", argv[2]);
    write(fd, cmd, strlen(cmd));

    for (int i = 3; i < argc; i++) {
        if (strcmp(argv[i], "netmask") == 0 && i + 1 < argc) {
            snprintf(cmd, sizeof(cmd), "mask %s\n", argv[i + 1]);
            write(fd, cmd, strlen(cmd));
            i++;
        } else if (strcmp(argv[i], "gw") == 0 && i + 1 < argc) {
            snprintf(cmd, sizeof(cmd), "gw %s\n", argv[i + 1]);
            write(fd, cmd, strlen(cmd));
            i++;
        }
    }

    close(fd);
    return 0;
}

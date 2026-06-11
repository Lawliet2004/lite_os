#include "libc_lite.h"

int main(int argc, char **argv)
{
    (void)argc; (void)argv;
    int sock = socket(2, 2, 0); // AF_INET, SOCK_DGRAM
    if (sock < 0) {
        printf("udp_echo: socket creation failed\n");
        return 1;
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = 2; // AF_INET
    addr.sin_port = 0x0F27; // Port 9999 (byte-swapped 0x270F)
    addr.sin_addr.s_addr = 0; // INADDR_ANY

    if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        printf("udp_echo: bind failed\n");
        close(sock);
        return 1;
    }

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

    close(sock);
    return 0;
}

#include "libc_lite.h"

int main(int argc, char **argv)
{
    (void)argc; (void)argv;
    int server_fd = socket(2, 1, 0); // AF_INET, SOCK_STREAM
    if (server_fd < 0) {
        printf("http_server: socket creation failed\n");
        return 1;
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = 2; // AF_INET
    addr.sin_port = 0x5000; // Port 80 (byte-swapped 0x0050)
    addr.sin_addr.s_addr = 0; // INADDR_ANY

    if (bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        printf("http_server: bind failed\n");
        close(server_fd);
        return 1;
    }

    if (listen(server_fd, 5) < 0) {
        printf("http_server: listen failed\n");
        close(server_fd);
        return 1;
    }

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

    close(server_fd);
    return 0;
}

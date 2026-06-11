#include "libc_lite.h"
#include <stdbool.h>

// Re-use DNS resolve function
extern int resolve_host(const char *hostname, char *out_ip, size_t out_max);

static inline uint16_t swap16(uint16_t v) { return (v << 8) | (v >> 8); }

static bool parse_url(const char *url, char *host, int max_host, int *port, char *path, int max_path)
{
    const char *p = url;
    if (strncmp(p, "http://", 7) == 0) {
        p += 7;
    }

    // Extract host
    int host_len = 0;
    while (*p && *p != '/' && *p != ':') {
        if (host_len < max_host - 1) {
            host[host_len++] = *p;
        }
        p++;
    }
    host[host_len] = '\0';

    // Extract port
    *port = 80;
    if (*p == ':') {
        p++;
        int val = 0;
        while (*p >= '0' && *p <= '9') {
            val = val * 10 + (*p - '0');
            p++;
        }
        if (val > 0) *port = val;
    }

    // Extract path
    if (*p == '/') {
        strncpy(path, p, max_path - 1);
        path[max_path - 1] = '\0';
    } else {
        strcpy(path, "/");
    }

    return host_len > 0;
}

int main(int argc, char **argv)
{
    if (argc < 3) {
        printf("Usage: http_client <url> <output_file>\n");
        return 1;
    }

    const char *url = argv[1];
    const char *out_path = argv[2];

    char host[128];
    int port;
    char path[256];

    if (!parse_url(url, host, sizeof(host), &port, path, sizeof(path))) {
        printf("HTTP: invalid URL format\n");
        return 1;
    }

    char ip[64];
    printf("Resolving host %s...\n", host);
    if (resolve_host(host, ip, sizeof(ip)) != 0) {
        printf("HTTP: failed to resolve host '%s'\n", host);
        return 1;
    }
    printf("Connecting to %s (%s) on port %d...\n", host, ip, port);

    // Convert IP to bytes
    uint8_t ip_bytes[4] = {0};
    const char *p = ip;
    for (int i = 0; i < 4; i++) {
        int val = 0;
        while (*p >= '0' && *p <= '9') {
            val = val * 10 + (*p - '0');
            p++;
        }
        ip_bytes[i] = val;
        if (i < 3 && *p == '.') p++;
    }

    int sock = socket(2, 1, 0); // AF_INET, SOCK_STREAM
    if (sock < 0) {
        printf("HTTP: failed to create socket\n");
        return 1;
    }

    struct sockaddr_in dest;
    memset(&dest, 0, sizeof(dest));
    dest.sin_family = 2;
    dest.sin_port = swap16((uint16_t)port);
    memcpy(&dest.sin_addr.s_addr, ip_bytes, 4);

    if (connect(sock, (struct sockaddr *)&dest, sizeof(dest)) < 0) {
        printf("HTTP: failed to connect to server\n");
        close(sock);
        return 1;
    }

    // Send HTTP GET
    char request[1024];
    snprintf(request, sizeof(request),
             "GET %s HTTP/1.0\r\n"
             "Host: %s\r\n"
             "User-Agent: LiteNixHTTP/1.0\r\n"
             "Connection: close\r\n\r\n",
             path, host);

    ssize_t req_len = strlen(request);
    if (write(sock, request, req_len) != req_len) {
        printf("HTTP: failed to send request\n");
        close(sock);
        return 1;
    }

    // Read headers
    char header_buf[4096];
    int header_len = 0;
    char *body_ptr = NULL;

    while (header_len < (int)sizeof(header_buf) - 1) {
        char c;
        ssize_t r = read(sock, &c, 1);
        if (r <= 0) break;
        header_buf[header_len++] = c;
        header_buf[header_len] = '\0';

        // Check for double CRLF or double LF
        if (header_len >= 4 &&
            header_buf[header_len - 4] == '\r' && header_buf[header_len - 3] == '\n' &&
            header_buf[header_len - 2] == '\r' && header_buf[header_len - 1] == '\n') {
            body_ptr = header_buf + header_len;
            break;
        }
        if (header_len >= 2 &&
            header_buf[header_len - 2] == '\n' && header_buf[header_len - 1] == '\n') {
            body_ptr = header_buf + header_len;
            break;
        }
    }

    if (!body_ptr) {
        printf("HTTP: failed to parse HTTP headers (response too large or invalid)\n");
        close(sock);
        return 1;
    }

    // Check status line
    if (strncmp(header_buf, "HTTP/1.", 7) != 0) {
        printf("HTTP: invalid protocol\n");
        close(sock);
        return 1;
    }

    char *status = strchr(header_buf, ' ');
    if (!status) {
        printf("HTTP: invalid status line\n");
        close(sock);
        return 1;
    }
    status++;
    int code = 0;
    while (*status >= '0' && *status <= '9') {
        code = code * 10 + (*status - '0');
        status++;
    }

    if (code != 200) {
        printf("HTTP: server returned non-200 status code: %d\n", code);
        close(sock);
        return 1;
    }

    // Parse content length
    long content_length = -1;
    char *cl_header = header_buf;
    while (cl_header) {
        cl_header = strchr(cl_header, '\n');
        if (!cl_header) break;
        cl_header++; // Skip '\n'
        if (strncmp(cl_header, "Content-Length:", 15) == 0 || strncmp(cl_header, "content-length:", 15) == 0) {
            char *val = cl_header + 15;
            while (*val == ' ' || *val == '\t') val++;
            content_length = 0;
            while (*val >= '0' && *val <= '9') {
                content_length = content_length * 10 + (*val - '0');
                val++;
            }
            break;
        }
    }

    // Open output file
    int out_fd = open(out_path, O_WRONLY | O_CREAT | O_TRUNC);
    if (out_fd < 0) {
        printf("HTTP: failed to open output file '%s'\n", out_path);
        close(sock);
        return 1;
    }

    printf("Downloading %ld bytes...\n", content_length);

    char chunk[4096];
    ssize_t n;
    long total_read = 0;
    while ((n = read(sock, chunk, sizeof(chunk))) > 0) {
        write(out_fd, chunk, n);
        total_read += n;
    }

    close(out_fd);
    close(sock);

    if (content_length >= 0 && total_read != content_length) {
        printf("HTTP: warning: short read (expected %ld, got %ld)\n", content_length, total_read);
    } else {
        printf("HTTP: download completed successfully (%ld bytes)\n", total_read);
    }

    return 0;
}

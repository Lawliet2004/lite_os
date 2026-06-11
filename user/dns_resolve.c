#include "libc_lite.h"
#include <stdbool.h>

struct dns_header {
    uint16_t id;
    uint16_t flags;
    uint16_t qdcount;
    uint16_t ancount;
    uint16_t nscount;
    uint16_t arcount;
} __attribute__((packed));

static inline uint16_t swap16(uint16_t v) { return (v << 8) | (v >> 8); }

static void get_dns_server(char *dns_ip, size_t max_len)
{
    strncpy(dns_ip, "10.0.2.3", max_len);

    int fd = open("/etc/resolv.conf", O_RDONLY);
    if (fd < 0) return;

    char buf[1024];
    ssize_t n = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (n <= 0) return;
    buf[n] = '\0';

    char *line = buf;
    while (*line) {
        char *next = strchr(line, '\n');
        if (next) *next = '\0';

        if (strncmp(line, "nameserver", 10) == 0) {
            char *ip = line + 10;
            while (*ip == ' ' || *ip == '\t') ip++;
            char *end = ip;
            while (*end && *end != ' ' && *end != '\t' && *end != '\r' && *end != '\n') end++;
            *end = '\0';
            if (*ip) {
                strncpy(dns_ip, ip, max_len);
                break;
            }
        }

        if (next) line = next + 1;
        else break;
    }
}

static int parse_name(const uint8_t *buffer, int size, int *offset, char *out, int out_max, int depth)
{
    if (depth > 10) return -1;
    int curr = *offset;
    int written = 0;
    bool jumped = false;
    int original_offset = -1;

    while (curr < size) {
        uint8_t len = buffer[curr];
        if (len == 0) {
            curr++;
            if (!jumped) {
                *offset = curr;
            }
            if (written > 0 && out[written - 1] == '.') {
                out[written - 1] = '\0';
            } else {
                out[written] = '\0';
            }
            return 0;
        }

        if ((len & 0xC0) == 0xC0) {
            if (curr + 1 >= size) return -1;
            uint16_t ptr = ((len & 0x3F) << 8) | buffer[curr + 1];
            if (ptr >= size) return -1;
            if (!jumped) {
                original_offset = curr + 2;
                jumped = true;
            }
            curr = ptr;
            int dummy = curr;
            if (parse_name(buffer, size, &dummy, out + written, out_max - written, depth + 1) < 0) {
                return -1;
            }
            if (jumped) {
                *offset = original_offset;
            }
            return 0;
        } else {
            curr++;
            if (curr + len > size) return -1;
            if (written + len + 1 >= out_max) return -1;
            memcpy(out + written, buffer + curr, len);
            written += len;
            out[written++] = '.';
            curr += len;
        }
    }
    return -1;
}

static bool resolve_hosts_file(const char *hostname, char *out_ip, size_t out_max)
{
    int fd = open("/etc/hosts", O_RDONLY);
    if (fd < 0) return false;

    char buf[1024];
    ssize_t n = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (n <= 0) return false;
    buf[n] = '\0';

    char *line = buf;
    while (*line) {
        char *next = strchr(line, '\n');
        if (next) *next = '\0';

        char *p = line;
        while (*p == ' ' || *p == '\t') p++;

        if (*p && *p != '#') {
            char *ip = p;
            while (*p && *p != ' ' && *p != '\t') p++;
            if (*p) {
                *p = '\0';
                p++;
                while (*p == ' ' || *p == '\t') p++;
                char *host = p;
                char *end = host;
                while (*end && *end != ' ' && *end != '\t' && *end != '\r' && *end != '\n') end++;
                *end = '\0';

                if (strcmp(host, hostname) == 0) {
                    strncpy(out_ip, ip, out_max);
                    return true;
                }
            }
        }

        if (next) line = next + 1;
        else break;
    }
    return false;
}

int resolve_host(const char *hostname, char *out_ip, size_t out_max)
{
    if (resolve_hosts_file(hostname, out_ip, out_max)) {
        return 0;
    }

    int ip_bytes[4];
    int parsed = 0;
    const char *p = hostname;
    while (*p) {
        if (*p < '0' || *p > '9') break;
        int val = 0;
        while (*p >= '0' && *p <= '9') {
            val = val * 10 + (*p - '0');
            p++;
        }
        if (val > 255) break;
        ip_bytes[parsed++] = val;
        if (parsed == 4) {
            if (*p == '\0') {
                strncpy(out_ip, hostname, out_max);
                return 0;
            }
            break;
        }
        if (*p != '.') break;
        p++;
    }

    char dns_ip_str[64];
    get_dns_server(dns_ip_str, sizeof(dns_ip_str));

    int sock = socket(2, 2, 0); // AF_INET, SOCK_DGRAM
    if (sock < 0) return -1;

    uint8_t dns_ip[4] = {0};
    p = dns_ip_str;
    for (int i = 0; i < 4; i++) {
        int val = 0;
        while (*p >= '0' && *p <= '9') {
            val = val * 10 + (*p - '0');
            p++;
        }
        dns_ip[i] = val;
        if (i < 3 && *p == '.') p++;
    }

    struct sockaddr_in dest;
    memset(&dest, 0, sizeof(dest));
    dest.sin_family = 2;
    dest.sin_port = swap16(53);
    memcpy(&dest.sin_addr.s_addr, dns_ip, 4);

    uint8_t packet[512];
    memset(packet, 0, sizeof(packet));

    struct dns_header *dns = (struct dns_header *)packet;
    dns->id = swap16(0x1234);
    dns->flags = swap16(0x0100);
    dns->qdcount = swap16(1);

    int offset = sizeof(struct dns_header);
    const char *start = hostname;
    while (*start) {
        const char *end = strchr(start, '.');
        if (!end) end = start + strlen(start);
        int len = end - start;
        if (len <= 0 || len > 63 || offset + 1 + len >= 512) {
            close(sock);
            return -1;
        }
        packet[offset++] = len;
        memcpy(packet + offset, start, len);
        offset += len;
        if (*end == '.') {
            start = end + 1;
        } else {
            break;
        }
    }
    packet[offset++] = 0;

    if (offset + 4 >= 512) {
        close(sock);
        return -1;
    }
    packet[offset++] = 0;
    packet[offset++] = 1;
    packet[offset++] = 0;
    packet[offset++] = 1;

    if (sendto(sock, packet, offset, 0, (struct sockaddr *)&dest, sizeof(dest)) != offset) {
        close(sock);
        return -1;
    }

    struct pollfd pfd;
    pfd.fd = sock;
    pfd.events = POLLIN;
    int poll_res = poll(&pfd, 1, 5000);
    if (poll_res <= 0) {
        close(sock);
        return -1;
    }

    uint8_t resp[1024];
    struct sockaddr_in from;
    socklen_t from_len = sizeof(from);
    ssize_t resp_len = recvfrom(sock, resp, sizeof(resp), 0, (struct sockaddr *)&from, &from_len);
    close(sock);

    if (resp_len < (ssize_t)sizeof(struct dns_header)) return -1;

    struct dns_header *resp_dns = (struct dns_header *)resp;
    if (resp_dns->id != swap16(0x1234)) return -1;

    uint16_t flags = swap16(resp_dns->flags);
    if ((flags & 0x8000) == 0) return -1;
    if ((flags & 0x000F) != 0) return -1;

    uint16_t qdcount = swap16(resp_dns->qdcount);
    uint16_t ancount = swap16(resp_dns->ancount);

    int r_off = sizeof(struct dns_header);
    for (int i = 0; i < qdcount; i++) {
        char dummy_name[256];
        if (parse_name(resp, resp_len, &r_off, dummy_name, sizeof(dummy_name), 0) < 0) {
            return -1;
        }
        r_off += 4;
        if (r_off > resp_len) return -1;
    }

    for (int i = 0; i < ancount; i++) {
        char ans_name[256];
        if (parse_name(resp, resp_len, &r_off, ans_name, sizeof(ans_name), 0) < 0) {
            return -1;
        }
        if (r_off + 10 > resp_len) return -1;

        uint16_t type = (resp[r_off] << 8) | resp[r_off + 1];
        uint16_t class = (resp[r_off + 2] << 8) | resp[r_off + 3];
        r_off += 8;
        uint16_t rdlength = (resp[r_off] << 8) | resp[r_off + 1];
        r_off += 2;

        if (r_off + rdlength > resp_len) return -1;

        if (type == 1 && class == 1 && rdlength == 4) {
            snprintf(out_ip, out_max, "%d.%d.%d.%d",
                     resp[r_off], resp[r_off + 1], resp[r_off + 2], resp[r_off + 3]);
            return 0;
        }
        r_off += rdlength;
    }

    return -1;
}

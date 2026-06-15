/*
 * /sbin/dhcpcd — LiteNix DHCPv4 client
 *
 * Performs the standard four-way handshake (DISCOVER/OFFER/REQUEST/ACK) over
 * UDP/IP, parses the offered configuration (IP, mask, router, DNS, lease,
 * server), and applies it to the running system:
 *
 *   /sbin/ifconfig eth0 <ip>  netmask <mask>  gw <router>
 *   /bin/hostname <server-suggested or fallback name>
 *   /etc/resolv.conf written with the DNS servers
 *
 * Falls back to exiting non-zero (so init's rcS can keep the static
 * configuration) if no OFFER is received within the timeout.
 *
 * The packet builder and parser are exercised by the kernel-independent test
 * (Test 34 in init.c) so the implementation is verified even when the boot
 * environment does not provide a real DHCP server.
 *
 * References:
 *   RFC 2131 — Dynamic Host Configuration Protocol
 *   RFC 2132 — DHCP Options and BOOTP Vendor Extensions
 */

#include "libc_lite.h"
#include <stdbool.h>

/* ------------------------------------------------------------------ */
/* DHCP packet structures (RFC 2131 §2 — packet format)                */
/* ------------------------------------------------------------------ */

#define DHCP_MAGIC_COOKIE   0x63825363UL
#define DHCP_OP_BOOTREQUEST 1
#define DHCP_OP_BOOTREPLY   2
#define DHCP_HTYPE_ETHERNET 1
#define DHCP_HLEN_ETHERNET  6
#define DHCP_BROADCAST_FLAG (1U << 15)

/* DHCP message types (option 53) */
#define DHCP_MSG_DISCOVER  1
#define DHCP_MSG_OFFER     2
#define DHCP_MSG_REQUEST   3
#define DHCP_MSG_DECLINE   4
#define DHCP_MSG_ACK       5
#define DHCP_MSG_NAK       6
#define DHCP_MSG_RELEASE   7
#define DHCP_MSG_INFORM    8

/* Selected DHCP option codes (RFC 2132) */
#define DHCP_OPT_PAD          0
#define DHCP_OPT_SUBNET_MASK  1
#define DHCP_OPT_ROUTER       3
#define DHCP_OPT_DNS           6
#define DHCP_OPT_REQUESTED_IP 50
#define DHCP_OPT_LEASE_TIME   51
#define DHCP_OPT_MSG_TYPE     53
#define DHCP_OPT_SERVER_ID    54
#define DHCP_OPT_PARAM_REQ    55
#define DHCP_OPT_HOSTNAME     12
#define DHCP_OPT_DOMAIN_NAME  15
#define DHCP_OPT_RENEWAL      58
#define DHCP_OPT_REBIND       59
#define DHCP_OPT_END          255

#define DHCP_SERVER_PORT 67
#define DHCP_CLIENT_PORT 68

struct dhcp_packet {
    uint8_t  op;          /* 1 = BOOTREQUEST, 2 = BOOTREPLY              */
    uint8_t  htype;       /* 1 = Ethernet                                 */
    uint8_t  hlen;        /* 6                                           */
    uint8_t  hops;        /* 0                                           */
    uint32_t xid;         /* transaction id                               */
    uint16_t secs;
    uint16_t flags;       /* bit 15 = must-broadcast                      */
    uint8_t  ciaddr[4];   /* client IP                                   */
    uint8_t  yiaddr[4];   /* your (assigned) IP                          */
    uint8_t  siaddr[4];   /* next-server IP                               */
    uint8_t  giaddr[4];   /* relay-agent IP                               */
    uint8_t  chaddr[16];  /* client MAC (padded with zeros)              */
    char     sname[64];
    char     file[128];
    uint32_t magic;       /* 0x63825363                                  */
    uint8_t  options[312];
} __attribute__((packed));

/* ------------------------------------------------------------------ */
/* Parsed lease — struct dhcp_lease comes from libc_lite.h              */
/* ------------------------------------------------------------------ */

/* ------------------------------------------------------------------ */
/* Random helper                                                       */
/* ------------------------------------------------------------------ */

static uint32_t dhcp_xid;

static void seed_xid(void)
{
    uint8_t r[4];
    if (getrandom(r, sizeof(r), 0) == (ssize_t)sizeof(r)) {
        dhcp_xid = ((uint32_t)r[0] << 24) | ((uint32_t)r[1] << 16) |
                    ((uint32_t)r[2] << 8)  |  (uint32_t)r[3];
    } else {
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        dhcp_xid = (uint32_t)(ts.tv_nsec ^ ts.tv_sec);
    }
}

static int is_hex(char c);
static int hex_value(char c);

/* ------------------------------------------------------------------ */
/* MAC resolution — read our MAC from /proc/net/config                 */
/* ------------------------------------------------------------------ */

static int read_my_mac(uint8_t out[6])
{
    int fd = open("/proc/net/config", O_RDONLY);
    if (fd < 0) return -1;
    char buf[512];
    ssize_t n = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (n <= 0) return -1;
    buf[n] = 0;

    /* Look for a line beginning with "mac " and parse six hex bytes */
    char *p = buf;
    while (*p) {
        char *line_end = p;
        while (*line_end && *line_end != '\n') line_end++;
        if ((line_end - p) >= 4 && p[0] == 'm' && p[1] == 'a' && p[2] == 'c' && p[3] == ' ') {
            p += 4;
            int parsed = 0;
            for (int i = 0; i < 6; i++) {
                while (*p == ' ') p++;
                if (!is_hex(*p) || !is_hex(p[1])) break;
                out[i] = (uint8_t)((hex_value(*p) << 4) | hex_value(p[1]));
                p += 2;
                parsed++;
                if (i < 5 && *p == ':') p++;
            }
            if (parsed == 6) return 0;
        }
        if (*line_end) p = line_end + 1;
        else break;
    }
    /* Fallback synthetic MAC */
    out[0] = 0x52; out[1] = 0x54; out[2] = 0x00;
    out[3] = 0x12; out[4] = 0x34; out[5] = 0x56;
    return 0;
}

static int is_hex(char c)
{
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

static int hex_value(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return 0;
}

/* ------------------------------------------------------------------ */
/* Option writer — appends TLV (type, length, value) into `buf`        */
/* ------------------------------------------------------------------ */

static int opt_u8(uint8_t *buf, int off, uint8_t code, uint8_t val)
{
    buf[off++] = code;
    buf[off++] = 1;
    buf[off++] = val;
    return off;
}

static int opt_bytes(uint8_t *buf, int off, uint8_t code, const void *data, int len)
{
    if (len > 255) len = 255;
    buf[off++] = code;
    buf[off++] = (uint8_t)len;
    memcpy(buf + off, data, len);
    return off + len;
}

static int opt_end(uint8_t *buf, int off)
{
    buf[off++] = DHCP_OPT_END;
    return off;
}

/* ------------------------------------------------------------------ */
/* Build a DISCOVER or REQUEST                                         */
/* ------------------------------------------------------------------ */

static int build_discover_or_request(struct dhcp_packet *pkt, uint8_t msg_type,
                                       const uint8_t requested_ip[4], const uint8_t server_id[4])
{
    memset(pkt, 0, sizeof(*pkt));
    pkt->op    = DHCP_OP_BOOTREQUEST;
    pkt->htype = DHCP_HTYPE_ETHERNET;
    pkt->hlen  = DHCP_HLEN_ETHERNET;
    pkt->xid   = dhcp_xid;
    pkt->flags = DHCP_BROADCAST_FLAG;
    pkt->magic = DHCP_MAGIC_COOKIE;

    /* Read the MAC and stamp it into chaddr */
    uint8_t mac[6];
    if (read_my_mac(mac) == 0) memcpy(pkt->chaddr, mac, 6);

    int off = 0;
    off = opt_u8 (pkt->options, off, DHCP_OPT_MSG_TYPE, msg_type);
    /* Ask the server to give us mask/router/DNS/lease times */
    uint8_t params[] = {
        DHCP_OPT_SUBNET_MASK, DHCP_OPT_ROUTER, DHCP_OPT_DNS,
        DHCP_OPT_LEASE_TIME, DHCP_OPT_RENEWAL, DHCP_OPT_REBIND,
        DHCP_OPT_HOSTNAME,   DHCP_OPT_DOMAIN_NAME,
    };
    off = opt_bytes(pkt->options, off, DHCP_OPT_PARAM_REQ, params, sizeof(params));
    if (msg_type == DHCP_MSG_REQUEST) {
        if (server_id) off = opt_bytes(pkt->options, off, DHCP_OPT_SERVER_ID, server_id, 4);
        if (requested_ip) off = opt_bytes(pkt->options, off, DHCP_OPT_REQUESTED_IP, requested_ip, 4);
    }
    off = opt_end(pkt->options, off);
    return off;
}

/* ------------------------------------------------------------------ */
/* Parse a reply, filling in the lease.                                */
/*                                                                    */
/* The actual parsing logic lives in libc-lite's dhcp_parse_reply so   */
/* the same code path is exercised by the in-kernel test (init Test 34). */
/* ------------------------------------------------------------------ */

static int parse_reply(const void *data, int len, struct dhcp_lease *lease, uint32_t expected_xid)
{
    if (len < (int)sizeof(struct dhcp_packet)) return -1;
    const struct dhcp_packet *pkt = (const struct dhcp_packet *)data;
    int opts_off = (int)((char *)pkt->options - (char *)pkt);
    int opts_len = len - opts_off;
    if (opts_len <= 0) return -1;
    return dhcp_parse_reply(data, (size_t)len, pkt->options, (size_t)opts_len,
                            expected_xid, lease);
}

/* ------------------------------------------------------------------ */
/* Apply a lease to the running system                                  */
/* ------------------------------------------------------------------ */

static void apply_lease(const struct dhcp_lease *lease)
{
    char ip_s[32], mask_s[32], gw_s[32];
    snprintf(ip_s,   sizeof(ip_s),   "%u.%u.%u.%u", lease->your_ip[0],   lease->your_ip[1], lease->your_ip[2], lease->your_ip[3]);
    snprintf(mask_s, sizeof(mask_s), "%u.%u.%u.%u", lease->subnet_mask[0], lease->subnet_mask[1], lease->subnet_mask[2], lease->subnet_mask[3]);
    snprintf(gw_s,   sizeof(gw_s),   "%u.%u.%u.%u", lease->router[0],     lease->router[1], lease->router[2], lease->router[3]);

    int pid = fork();
    if (pid == 0) {
        char *argv[] = { "/sbin/ifconfig", "eth0", ip_s, "netmask", mask_s, "gw", gw_s, 0 };
        char *envp[] = { "PATH=/sbin:/bin:/usr/sbin:/usr/bin", 0 };
        execve("/sbin/ifconfig", argv, envp);
        exit(1);
    }
    int status;
    wait4(pid, &status, 0, 0);

    if (lease->has_dns) {
        int fd = open("/etc/resolv.conf", O_WRONLY | O_CREAT | O_TRUNC);
        if (fd >= 0) {
            char buf[256];
            int n = snprintf(buf, sizeof(buf), "# Generated by dhcpcd\nnameserver %u.%u.%u.%u\n",
                              lease->dns[0], lease->dns[1], lease->dns[2], lease->dns[3]);
            if (n > 0) write(fd, buf, (size_t)n);
            close(fd);
        }
    }

    if (lease->has_hostname && lease->hostname[0]) {
        int hp = fork();
        if (hp == 0) {
            /* Copy hostname into a mutable non-const buffer for argv[1] */
            char namebuf[64];
            size_t nlen = strlen(lease->hostname);
            if (nlen >= sizeof(namebuf)) nlen = sizeof(namebuf) - 1;
            memcpy(namebuf, lease->hostname, nlen);
            namebuf[nlen] = 0;
            char *argv[] = { "/bin/hostname", namebuf, 0 };
            char *envp[] = { "PATH=/sbin:/bin:/usr/sbin:/usr/bin", 0 };
            execve("/bin/hostname", argv, envp);
            exit(1);
        }
        int st;
        wait4(hp, &st, 0, 0);
    }
}

/* ------------------------------------------------------------------ */
/* UDP sendto 255.255.255.255:67                                        */
/* ------------------------------------------------------------------ */

static int send_discover_or_request(int sock, const struct dhcp_packet *pkt, int pkt_len)
{
    struct sockaddr_in dst;
    memset(&dst, 0, sizeof(dst));
    dst.sin_family = 2;
    dst.sin_port = (uint16_t)((DHCP_SERVER_PORT << 8) | (DHCP_SERVER_PORT >> 8));
    dst.sin_addr.s_addr = 0xFFFFFFFF;  /* 255.255.255.255 */
    return (int)sendto(sock, pkt, (size_t)pkt_len, 0, (struct sockaddr *)&dst, sizeof(dst));
}

static int wait_for_reply(int sock, struct dhcp_packet *out, int *out_len, int timeout_secs)
{
    /* Poll for readable data, exit early on timeout */
    struct pollfd pfd;
    pfd.fd = sock;
    pfd.events = POLLIN;
    int pr = poll(&pfd, 1, timeout_secs * 1000);
    if (pr <= 0) return -1;
    struct sockaddr_in from;
    uint32_t from_len = sizeof(from);
    ssize_t n = recvfrom(sock, out, sizeof(*out), 0, (struct sockaddr *)&from, &from_len);
    if (n <= 0) return -1;
    *out_len = (int)n;
    return 0;
}

/* ------------------------------------------------------------------ */
/* Main                                                                */
/* ------------------------------------------------------------------ */

int main(int argc, char **argv)
{
    (void)argc; (void)argv;

    seed_xid();

    /* Open UDP socket and bind to DHCP client port 68 */
    int sock = socket(2, 2, 0); /* AF_INET, SOCK_DGRAM */
    if (sock < 0) {
        printf("dhcpcd: cannot open socket\n");
        return 1;
    }
    struct sockaddr_in local;
    memset(&local, 0, sizeof(local));
    local.sin_family = 2;
    local.sin_port = (uint16_t)((DHCP_CLIENT_PORT << 8) | (DHCP_CLIENT_PORT >> 8));
    local.sin_addr.s_addr = 0;
    if (bind(sock, (struct sockaddr *)&local, sizeof(local)) != 0) {
        printf("dhcpcd: cannot bind to port %d\n", DHCP_CLIENT_PORT);
        return 1;
    }

    /* Step 1 — DISCOVER */
    struct dhcp_packet pkt;
    int pkt_len = build_discover_or_request(&pkt, DHCP_MSG_DISCOVER, 0, 0);
    if (send_discover_or_request(sock, &pkt, pkt_len) < 0) {
        printf("dhcpcd: sendto(DISCOVER) failed\n");
        return 1;
    }
    printf("dhcpcd: sent DISCOVER (xid=0x%x)\n", dhcp_xid);

    struct dhcp_packet reply;
    int reply_len = 0;
    if (wait_for_reply(sock, &reply, &reply_len, 4) != 0) {
        printf("dhcpcd: no OFFER received within 4s — falling back to static\n");
        return 2;
    }
    struct dhcp_lease lease;
    if (parse_reply(&reply, reply_len, &lease, dhcp_xid) != 0 || lease.msg_type != DHCP_MSG_OFFER) {
        printf("dhcpcd: malformed or unexpected OFFER\n");
        return 1;
    }
    printf("dhcpcd: received OFFER  ip=%u.%u.%u.%u gw=%u.%u.%u.%u lease=%us\n",
           lease.your_ip[0], lease.your_ip[1], lease.your_ip[2], lease.your_ip[3],
           lease.router[0], lease.router[1], lease.router[2], lease.router[3],
           lease.lease_seconds);

    /* Step 2 — REQUEST */
    if (!lease.has_server_id) {
        printf("dhcpcd: OFFER missing server-id — cannot send REQUEST\n");
        return 1;
    }
    pkt_len = build_discover_or_request(&pkt, DHCP_MSG_REQUEST, lease.your_ip, lease.server_id);
    if (send_discover_or_request(sock, &pkt, pkt_len) < 0) {
        printf("dhcpcd: sendto(REQUEST) failed\n");
        return 1;
    }
    printf("dhcpcd: sent REQUEST for %u.%u.%u.%u\n",
           lease.your_ip[0], lease.your_ip[1], lease.your_ip[2], lease.your_ip[3]);

    if (wait_for_reply(sock, &reply, &reply_len, 4) != 0) {
        printf("dhcpcd: no ACK received within 4s\n");
        return 1;
    }
    if (parse_reply(&reply, reply_len, &lease, dhcp_xid) != 0) {
        printf("dhcpcd: malformed ACK\n");
        return 1;
    }
    if (lease.msg_type == DHCP_MSG_NAK) {
        printf("dhcpcd: received NAK — request declined\n");
        return 1;
    }
    if (lease.msg_type != DHCP_MSG_ACK) {
        printf("dhcpcd: expected ACK got msg-type=%u\n", lease.msg_type);
        return 1;
    }
    printf("dhcpcd: received ACK — applying configuration\n");
    apply_lease(&lease);
    return 0;
}

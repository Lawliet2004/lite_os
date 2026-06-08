#include <net/net.h>
#include <lib/string.h>
#include <kernel/printk.h>

extern void net_ipv4_send(const uint8_t dest_ip[4], uint8_t proto, const void *payload, uint16_t len);

void net_icmp_receive(const uint8_t *src_ip, const void *data, uint16_t len)
{
    if (len < sizeof(struct icmp_hdr)) return;

    const struct icmp_hdr *icmp_req = (const struct icmp_hdr *)data;
    if (icmp_req->type == ICMP_TYPE_ECHO_REQUEST) {
        static uint8_t reply_buf[1600];
        if (len > sizeof(reply_buf)) return;
        printk("ICMP: ping received from %d.%d.%d.%d\n",
               src_ip[0], src_ip[1], src_ip[2], src_ip[3]);
        memcpy(reply_buf, data, len);

        struct icmp_hdr *icmp_rep = (struct icmp_hdr *)reply_buf;
        icmp_rep->type = ICMP_TYPE_ECHO_REPLY;
        icmp_rep->csum = 0;
        icmp_rep->csum = ip_checksum(icmp_rep, len);

        net_ipv4_send(src_ip, IP_PROTO_ICMP, reply_buf, len);
    }
}

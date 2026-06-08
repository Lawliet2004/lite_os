#include <net/net.h>
#include <sched/task.h>
#include <lib/string.h>

extern uint8_t my_ip[4];
extern void net_eth_send(const uint8_t dest_mac[6], uint16_t type, const void *payload, uint16_t len);
extern bool arp_resolve(const uint8_t ip[4], uint8_t mac_out[6]);

extern void net_icmp_receive(const uint8_t *src_ip, const void *data, uint16_t len);
extern void net_udp_receive(const uint8_t *src_ip, const void *data, uint16_t len);
extern void net_tcp_receive(const uint8_t *src_ip, const void *data, uint16_t len);

static uint16_t ip_id = 0;

void net_ipv4_send(const uint8_t dest_ip[4], uint8_t proto, const void *payload, uint16_t len)
{
    static uint8_t ip_buffer[1600];
    if (sizeof(struct ipv4_hdr) + len > sizeof(ip_buffer)) return;

    struct ipv4_hdr *ip = (struct ipv4_hdr *)ip_buffer;
    ip->ver_ihl = 0x45; // Version 4, IHL 5 (20 bytes)
    ip->tos = 0;
    ip->len = (uint16_t)(((sizeof(struct ipv4_hdr) + len) >> 8) | ((sizeof(struct ipv4_hdr) + len) << 8));
    ip->id = (uint16_t)((ip_id >> 8) | (ip_id << 8));
    ip_id++;
    ip->flags_frag = 0;
    ip->ttl = 64;
    ip->proto = proto;
    ip->csum = 0;
    memcpy(ip->src, my_ip, 4);
    memcpy(ip->dest, dest_ip, 4);

    ip->csum = ip_checksum(ip, sizeof(struct ipv4_hdr));

    memcpy(ip_buffer + sizeof(struct ipv4_hdr), payload, len);

    uint8_t dest_mac[6];
    int retries = 5;
    while (!arp_resolve(dest_ip, dest_mac) && retries-- > 0) {
        task_sleep_ticks(2); // Wait 2 ticks (20ms)
    }

    if (retries < 0) {
        return; // Drop packet if ARP could not resolve
    }

    net_eth_send(dest_mac, ETH_TYPE_IPV4, ip_buffer, sizeof(struct ipv4_hdr) + len);
}

void net_ipv4_receive(const void *data, uint16_t len)
{
    if (len < sizeof(struct ipv4_hdr)) return;

    const struct ipv4_hdr *ip = (const struct ipv4_hdr *)data;
    uint8_t ihl = (ip->ver_ihl & 0x0F) * 4;
    if (len < ihl) return;

    // Verify target IP is my_ip or broadcast (like 255.255.255.255 or subnet broadcast)
    if (memcmp(ip->dest, my_ip, 4) != 0 && ip->dest[3] != 255) {
        return;
    }

    const uint8_t *payload = (const uint8_t *)data + ihl;
    uint16_t payload_len = (uint16_t)((ip->len >> 8) | (ip->len << 8)) - ihl;

    if (ip->proto == IP_PROTO_ICMP) {
        net_icmp_receive(ip->src, payload, payload_len);
    } else if (ip->proto == IP_PROTO_UDP) {
        net_udp_receive(ip->src, payload, payload_len);
    } else if (ip->proto == IP_PROTO_TCP) {
        net_tcp_receive(ip->src, payload, payload_len);
    }
}

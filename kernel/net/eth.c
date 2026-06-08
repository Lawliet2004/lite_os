#include <net/net.h>
#include <drivers/virtio_net.h>
#include <lib/string.h>

extern uint8_t my_mac[6];

void net_eth_send(const uint8_t dest_mac[6], uint16_t type, const void *payload, uint16_t len)
{
    static uint8_t eth_buffer[1600];
    if (sizeof(struct eth_hdr) + len > sizeof(eth_buffer)) return;

    struct eth_hdr *eth = (struct eth_hdr *)eth_buffer;
    memcpy(eth->dest, dest_mac, ETH_ADDR_LEN);
    memcpy(eth->src, my_mac, ETH_ADDR_LEN);
    eth->type = (uint16_t)((type >> 8) | (type << 8)); // Little-to-big conversion

    memcpy(eth_buffer + sizeof(struct eth_hdr), payload, len);

    virtio_net_send(eth_buffer, sizeof(struct eth_hdr) + len);
}

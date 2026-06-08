#include <net/net.h>
#include <lib/string.h>

extern uint8_t my_mac[6];
extern uint8_t my_ip[4];
extern void net_eth_send(const uint8_t dest_mac[6], uint16_t type, const void *payload, uint16_t len);

struct arp_entry {
    uint8_t ip[4];
    uint8_t mac[6];
    bool valid;
};
#define ARP_TABLE_SIZE 32
static struct arp_entry arp_table[ARP_TABLE_SIZE];

static void arp_cache_add(const uint8_t ip[4], const uint8_t mac[6])
{
    // Search if already exists
    for (int i = 0; i < ARP_TABLE_SIZE; i++) {
        if (arp_table[i].valid && memcmp(arp_table[i].ip, ip, 4) == 0) {
            memcpy(arp_table[i].mac, mac, 6);
            return;
        }
    }
    // Find empty slot
    for (int i = 0; i < ARP_TABLE_SIZE; i++) {
        if (!arp_table[i].valid) {
            memcpy(arp_table[i].ip, ip, 4);
            memcpy(arp_table[i].mac, mac, 6);
            arp_table[i].valid = true;
            return;
        }
    }
    // Evict slot 0
    memcpy(arp_table[0].ip, ip, 4);
    memcpy(arp_table[0].mac, mac, 6);
    arp_table[0].valid = true;
}

bool arp_resolve(const uint8_t ip[4], uint8_t mac_out[6])
{
    // Broadcast IP check (like 255.255.255.255)
    if (ip[0] == 255 && ip[1] == 255 && ip[2] == 255 && ip[3] == 255) {
        memset(mac_out, 0xFF, 6);
        return true;
    }

    for (int i = 0; i < ARP_TABLE_SIZE; i++) {
        if (arp_table[i].valid && memcmp(arp_table[i].ip, ip, 4) == 0) {
            memcpy(mac_out, arp_table[i].mac, 6);
            return true;
        }
    }

    // Send ARP query (broadcast)
    struct arp_hdr arp;
    arp.hw_type = (uint16_t)((1 >> 8) | (1 << 8)); // Ethernet (1)
    arp.proto_type = (uint16_t)((ETH_TYPE_IPV4 >> 8) | (ETH_TYPE_IPV4 << 8));
    arp.hw_len = 6;
    arp.proto_len = 4;
    arp.opcode = (uint16_t)((ARP_OP_REQUEST >> 8) | (ARP_OP_REQUEST << 8));
    memcpy(arp.sender_mac, my_mac, 6);
    memcpy(arp.sender_ip, my_ip, 4);
    memset(arp.target_mac, 0x00, 6);
    memcpy(arp.target_ip, ip, 4);

    uint8_t broadcast_mac[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    net_eth_send(broadcast_mac, ETH_TYPE_ARP, &arp, sizeof(arp));

    return false;
}

void net_arp_receive(const void *data, uint16_t len)
{
    if (len < sizeof(struct arp_hdr)) return;

    const struct arp_hdr *arp = (const struct arp_hdr *)data;
    uint16_t opcode = (uint16_t)((arp->opcode >> 8) | (arp->opcode << 8));

    if (opcode == ARP_OP_REQUEST) {
        if (memcmp(arp->target_ip, my_ip, 4) == 0) {
            arp_cache_add(arp->sender_ip, arp->sender_mac);

            // Send reply
            struct arp_hdr reply;
            reply.hw_type = arp->hw_type;
            reply.proto_type = arp->proto_type;
            reply.hw_len = 6;
            reply.proto_len = 4;
            reply.opcode = (uint16_t)((ARP_OP_REPLY >> 8) | (ARP_OP_REPLY << 8));
            memcpy(reply.sender_mac, my_mac, 6);
            memcpy(reply.sender_ip, my_ip, 4);
            memcpy(reply.target_mac, arp->sender_mac, 6);
            memcpy(reply.target_ip, arp->sender_ip, 4);

            net_eth_send(arp->sender_mac, ETH_TYPE_ARP, &reply, sizeof(reply));
        }
    } else if (opcode == ARP_OP_REPLY) {
        if (memcmp(arp->target_ip, my_ip, 4) == 0) {
            arp_cache_add(arp->sender_ip, arp->sender_mac);
        }
    }
}

#include <net/net.h>
#include <drivers/virtio_net.h>
#include <kernel/printk.h>
#include <lib/string.h>

uint8_t my_mac[6] = {0};
uint8_t my_ip[4] = {10, 0, 2, 15};
uint8_t my_gateway[4] = {10, 0, 2, 2};
uint8_t my_mask[4] = {255, 255, 255, 0};

uint16_t ip_checksum(const void *data, size_t len)
{
    const uint16_t *buf = (const uint16_t *)data;
    uint32_t sum = 0;
    while (len > 1) {
        sum += *buf++;
        len -= 2;
    }
    if (len == 1) {
        sum += *(const uint8_t *)buf;
    }
    while (sum >> 16) {
        sum = (sum & 0xFFFF) + (sum >> 16);
    }
    return (uint16_t)(~sum);
}

#include <sched/task.h>

static void net_thread_entry(void)
{
    extern void net_poll(void);
    while (1) {
        net_poll();
        task_yield();
    }
}

void net_init(void)
{
    virtio_net_get_mac(my_mac);
    printk("Net: Initialized network protocol stack\n");
    printk("  MAC: %02x:%02x:%02x:%02x:%02x:%02x\n",
           my_mac[0], my_mac[1], my_mac[2],
           my_mac[3], my_mac[4], my_mac[5]);
    printk("  IP : %d.%d.%d.%d Subnet: %d.%d.%d.%d Gateway: %d.%d.%d.%d\n",
           my_ip[0], my_ip[1], my_ip[2], my_ip[3],
           my_mask[0], my_mask[1], my_mask[2], my_mask[3],
           my_gateway[0], my_gateway[1], my_gateway[2], my_gateway[3]);

    struct task *net_task = task_create("net_thread", net_thread_entry);
    if (net_task == 0) {
        printk("Net: ERROR: Failed to create net_thread\n");
    } else {
        if (current_task && current_task->process && net_task->process) {
            extern void process_unlink_child(struct process *parent, struct process *child);
            process_unlink_child(current_task->process, net_task->process);
            net_task->process->parent = 0;
        }
        printk("Net: Background network task started (TID=%llu)\n", net_task->tid);
    }
}

extern void net_arp_receive(const void *data, uint16_t len);
extern void net_ipv4_receive(const void *data, uint16_t len);

void net_process_packet(const void *data, uint16_t len)
{
    if (len < sizeof(struct eth_hdr)) return;

    const struct eth_hdr *eth = (const struct eth_hdr *)data;
    uint16_t eth_type = (uint16_t)((eth->type >> 8) | (eth->type << 8)); // Big-to-little conversion

    const uint8_t *payload = (const uint8_t *)data + sizeof(struct eth_hdr);
    uint16_t payload_len = len - sizeof(struct eth_hdr);

    if (eth_type == ETH_TYPE_ARP) {
        net_arp_receive(payload, payload_len);
    } else if (eth_type == ETH_TYPE_IPV4) {
        net_ipv4_receive(payload, payload_len);
    }
}

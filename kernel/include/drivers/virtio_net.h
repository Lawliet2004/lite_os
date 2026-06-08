#ifndef LITENIX_DRIVERS_VIRTIO_NET_H
#define LITENIX_DRIVERS_VIRTIO_NET_H

#include <stdint.h>
#include <stdbool.h>

#define VIRTQ_DESC_F_NEXT     1
#define VIRTQ_DESC_F_WRITE    2
#define VIRTQ_DESC_F_INDIRECT 4

struct virtq_desc {
    uint64_t addr;
    uint32_t len;
    uint16_t flags;
    uint16_t next;
} __attribute__((packed));

struct virtq_avail {
    uint16_t flags;
    uint16_t idx;
    uint16_t ring[];
} __attribute__((packed));

struct virtq_used_elem {
    uint32_t id;
    uint32_t len;
} __attribute__((packed));

struct virtq_used {
    uint16_t flags;
    uint16_t idx;
    struct virtq_used_elem ring[];
} __attribute__((packed));

struct virtqueue {
    uint32_t size;             // number of descriptors
    struct virtq_desc *desc;  // pointer to descriptor table
    struct virtq_avail *avail; // pointer to available ring
    struct virtq_used *used;   // pointer to used ring
    uint16_t last_used_idx;    // index in used ring we have processed up to
    uint16_t port_base;        // IO port base for notifications
    uint16_t queue_index;      // 0 for RX, 1 for TX
};

struct virtio_net_hdr {
    uint8_t flags;
    uint8_t gso_type;
    uint16_t hdr_len;
    uint16_t gso_size;
    uint16_t csum_start;
    uint16_t csum_offset;
} __attribute__((packed));

bool virtio_net_init(void);
void virtio_net_send(const void *data, uint16_t len);
void virtio_net_handle_irq(void);
void virtio_net_get_mac(uint8_t mac[6]);

#endif

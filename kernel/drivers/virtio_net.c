#include <drivers/virtio_net.h>
#include <drivers/pci.h>
#include <arch/x86_64/io.h>
#include <arch/x86_64/pic.h>
#include <arch/x86_64/limine.h>
#include <mm/pmm.h>
#include <kernel/printk.h>
#include <lib/string.h>

static inline void *phys_to_virt(phys_addr_t phys) {
    extern volatile struct limine_hhdm_request hhdm_request;
    if (hhdm_request.response == 0) return 0;
    return (void *)(uintptr_t)(hhdm_request.response->offset + phys);
}

#define VIRTIO_VENDOR_ID 0x1AF4
#define VIRTIO_NET_DEVICE_ID 0x1000

uint8_t virtio_net_irq = 0xFF;
static uint16_t virtio_net_io_base = 0;
static struct pci_device virtio_net_pci_dev;

static struct virtqueue rx_vq;
static struct virtqueue tx_vq;

static phys_addr_t tx_buf_phys = 0;
static uint8_t guest_mac[6] = {0};

/* Lockless packet queue for incoming packets */
struct rx_packet {
    uint8_t data[1600];
    uint16_t len;
};
#define RX_QUEUE_SIZE 128
static struct rx_packet rx_queue[RX_QUEUE_SIZE];
static volatile uint32_t rx_queue_head = 0;
static volatile uint32_t rx_queue_tail = 0;

void virtio_net_get_mac(uint8_t mac[6])
{
    memcpy(mac, guest_mac, 6);
}

extern void net_process_packet(const void *data, uint16_t len);

void net_poll(void)
{
    while (rx_queue_tail != rx_queue_head) {
        struct rx_packet *pkt = &rx_queue[rx_queue_tail];
        net_process_packet(pkt->data, pkt->len);
        rx_queue_tail = (rx_queue_tail + 1) % RX_QUEUE_SIZE;
    }
}

static bool configure_queue(struct virtqueue *vq, uint16_t io_base, uint16_t q_idx)
{
    // 1. Select the queue
    outw(io_base + 14, q_idx);
    io_wait();

    // 2. Read its size
    uint16_t size = inw(io_base + 12);
    if (size == 0) {
        return false;
    }

    // 3. Compute sizes and allocate page-aligned contiguous physical memory
    uint32_t desc_size = size * sizeof(struct virtq_desc);
    uint32_t avail_size = 6 + size * sizeof(uint16_t);
    uint32_t used_offset = (desc_size + avail_size + 4095) & ~4095;
    uint32_t used_size = 6 + size * sizeof(struct virtq_used_elem);
    uint32_t total_size = used_offset + used_size;
    uint32_t page_count = (total_size + 4095) / 4096;

    phys_addr_t page = pmm_alloc_pages_contiguous(page_count);
    if (page == 0) {
        return false;
    }

    // Zero out the allocated space
    memset(phys_to_virt(page), 0, page_count * 4096);

    vq->size = size;
    vq->desc = (struct virtq_desc *)phys_to_virt(page);
    vq->avail = (struct virtq_avail *)((uint8_t *)vq->desc + desc_size);
    vq->used = (struct virtq_used *)((uint8_t *)vq->desc + used_offset);
    vq->last_used_idx = 0;
    vq->port_base = io_base;
    vq->queue_index = q_idx;

    // 4. Map the queue PFN to the device
    outl(io_base + 8, (uint32_t)(page >> 12));
    io_wait();

    return true;
}

bool virtio_net_init(void)
{
    if (!pci_find_device(VIRTIO_VENDOR_ID, VIRTIO_NET_DEVICE_ID, &virtio_net_pci_dev)) {
        printk("VirtIO-net: Device not found\n");
        return false;
    }

    virtio_net_io_base = (uint16_t)(virtio_net_pci_dev.bar0 & ~1);
    uint16_t io_base = virtio_net_io_base;

    printk("VirtIO-net: Found device at IO base 0x%x, IRQ %d\n", io_base, virtio_net_pci_dev.irq_line);

    // 1. Reset device
    outb(io_base + 18, 0);
    io_wait();

    // 2. Set ACKNOWLEDGE & DRIVER status
    outb(io_base + 18, inb(io_base + 18) | 1); // ACKNOWLEDGE
    io_wait();
    outb(io_base + 18, inb(io_base + 18) | 2); // DRIVER
    io_wait();

    // 3. Negotiate MAC feature (VIRTIO_NET_F_MAC is bit 5)
    uint32_t features = inl(io_base + 0);
    if (features & (1 << 5)) {
        outl(io_base + 4, 1 << 5);
    } else {
        outl(io_base + 4, 0);
    }
    io_wait();

    // 4. Configure Virtqueues
    if (!configure_queue(&rx_vq, io_base, 0)) {
        printk("VirtIO-net: Failed to configure RX queue\n");
        return false;
    }
    if (!configure_queue(&tx_vq, io_base, 1)) {
        printk("VirtIO-net: Failed to configure TX queue\n");
        return false;
    }

    // 5. Populate RX descriptors with buffers
    for (uint32_t i = 0; i < rx_vq.size; i++) {
        phys_addr_t rx_buf = pmm_alloc_page();
        if (rx_buf == 0) {
            printk("VirtIO-net: Failed to allocate RX buffer\n");
            return false;
        }
        memset(phys_to_virt(rx_buf), 0, 4096);
        rx_vq.desc[i].addr = rx_buf;
        rx_vq.desc[i].len = 1600; // fits standard ethernet packet easily
        rx_vq.desc[i].flags = VIRTQ_DESC_F_WRITE;
        rx_vq.desc[i].next = 0;
        rx_vq.avail->ring[i] = (uint16_t)i;
    }
    rx_vq.avail->flags = 0;
    rx_vq.avail->idx = (uint16_t)rx_vq.size;

    // Notify card of RX queue availability
    outw(io_base + 16, 0);
    io_wait();

    // 6. Allocate persistent TX buffer page
    tx_buf_phys = pmm_alloc_page();
    if (tx_buf_phys == 0) {
        printk("VirtIO-net: Failed to allocate TX buffer\n");
        return false;
    }
    memset(phys_to_virt(tx_buf_phys), 0, 4096);

    // 7. Read MAC address
    for (int i = 0; i < 6; i++) {
        guest_mac[i] = inb(io_base + 20 + i);
    }
    printk("VirtIO-net: MAC Address is %02x:%02x:%02x:%02x:%02x:%02x\n",
           guest_mac[0], guest_mac[1], guest_mac[2],
           guest_mac[3], guest_mac[4], guest_mac[5]);

    // 8. Set DRIVER_OK status
    outb(io_base + 18, inb(io_base + 18) | 4);
    io_wait();

    // 9. Enable interrupt line
    virtio_net_irq = virtio_net_pci_dev.irq_line;
    pic_unmask_irq(2);
    pic_unmask_irq(virtio_net_irq);

    printk("VirtIO-net: Initialized successfully\n");
    return true;
}

void virtio_net_send(const void *data, uint16_t len)
{
    if (virtio_net_io_base == 0 || tx_buf_phys == 0) return;
    if (len > 1514) len = 1514; // Cap to MTU

    // Copy VirtIO net header (all zeros) and payload
    struct virtio_net_hdr hdr;
    memset(&hdr, 0, sizeof(hdr));

    uint8_t *tx_virt = (uint8_t *)phys_to_virt(tx_buf_phys);
    memcpy(tx_virt, &hdr, sizeof(hdr));
    memcpy(tx_virt + sizeof(hdr), data, len);

    // Setup descriptor 0 of TX queue
    tx_vq.desc[0].addr = tx_buf_phys;
    tx_vq.desc[0].len = sizeof(hdr) + len;
    tx_vq.desc[0].flags = 0; // Read-only for device
    tx_vq.desc[0].next = 0;

    uint16_t avail_idx = tx_vq.avail->idx;
    tx_vq.avail->ring[avail_idx % tx_vq.size] = 0;
    tx_vq.avail->idx = avail_idx + 1;

    // Notify device
    outw(virtio_net_io_base + 16, 1); // Notify queue 1
    io_wait();

    // Poll until packet is processed
    while (tx_vq.used->idx == tx_vq.last_used_idx) {
        __asm__ volatile ("pause");
    }
    tx_vq.last_used_idx++;
}

void virtio_net_handle_irq(void)
{
    if (virtio_net_io_base == 0) return;

    // Read ISR Status
    uint8_t isr = inb(virtio_net_io_base + 19);
    if (!(isr & 1)) {
        return; // Not a queue interrupt
    }

    // Process received packets
    while (rx_vq.used->idx != rx_vq.last_used_idx) {
        uint16_t idx = rx_vq.used->ring[rx_vq.last_used_idx % rx_vq.size].id;
        uint32_t len = rx_vq.used->ring[rx_vq.last_used_idx % rx_vq.size].len;

        phys_addr_t buf_phys = rx_vq.desc[idx].addr;
        uint8_t *buf_virt = (uint8_t *)phys_to_virt(buf_phys);

        uint32_t payload_len = len - sizeof(struct virtio_net_hdr);
        uint8_t *payload = buf_virt + sizeof(struct virtio_net_hdr);

        // Queue packet into lockless RX queue
        uint32_t next_head = (rx_queue_head + 1) % RX_QUEUE_SIZE;
        if (next_head != rx_queue_tail) {
            memcpy(rx_queue[rx_queue_head].data, payload, payload_len);
            rx_queue[rx_queue_head].len = (uint16_t)payload_len;
            rx_queue_head = next_head;
        }

        // Replenish descriptor
        rx_vq.desc[idx].addr = buf_phys;
        rx_vq.desc[idx].len = 1600;
        rx_vq.desc[idx].flags = VIRTQ_DESC_F_WRITE;
        rx_vq.desc[idx].next = 0;

        uint16_t avail_idx = rx_vq.avail->idx;
        rx_vq.avail->ring[avail_idx % rx_vq.size] = idx;
        rx_vq.avail->idx = avail_idx + 1;

        rx_vq.last_used_idx++;
    }

    // Notify card of replenished descriptors
    outw(virtio_net_io_base + 16, 0);
    io_wait();
}

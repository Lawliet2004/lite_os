#include <net/net.h>
#include <sched/wait_queue.h>
#include <lib/string.h>

extern struct socket socket_table[];
extern void net_ipv4_send(const uint8_t dest_ip[4], uint8_t proto, const void *payload, uint16_t len);

void net_udp_receive(const uint8_t *src_ip, const void *data, uint16_t len)
{
    if (len < sizeof(struct udp_hdr)) return;

    const struct udp_hdr *udp = (const struct udp_hdr *)data;
    uint16_t dest_port = (uint16_t)((udp->dest_port >> 8) | (udp->dest_port << 8));
    uint16_t src_port = (uint16_t)((udp->src_port >> 8) | (udp->src_port << 8));
    uint16_t udp_len = (uint16_t)((udp->len >> 8) | (udp->len << 8));
    if (len < udp_len || udp_len < sizeof(struct udp_hdr)) return;

    const uint8_t *payload = (const uint8_t *)data + sizeof(struct udp_hdr);
    uint16_t payload_len = udp_len - sizeof(struct udp_hdr);

    // Find destination socket
    for (int i = 0; i < 64; i++) {
        struct socket *sock = &socket_table[i];
        if (sock->valid && sock->type == SOCKET_TYPE_UDP && sock->local_port == dest_port) {
            // Buffer packet
            uint32_t meta_size = 8; // src_ip (4), src_port (2), len (2)
            uint32_t total_write = meta_size + payload_len;

            // Check if there is enough space in the circular buffer
            uint32_t free_space;
            uint32_t w = sock->rx_write_ptr;
            uint32_t r = sock->rx_read_ptr;
            if (w >= r) {
                free_space = sock->rx_buf_size - (w - r);
            } else {
                free_space = r - w;
            }

            if (free_space > total_write + 1) {
                // Write meta
                for (int k = 0; k < 4; k++) {
                    sock->rx_buf[sock->rx_write_ptr] = src_ip[k];
                    sock->rx_write_ptr = (sock->rx_write_ptr + 1) % sock->rx_buf_size;
                }
                sock->rx_buf[sock->rx_write_ptr] = (uint8_t)(src_port & 0xFF);
                sock->rx_write_ptr = (sock->rx_write_ptr + 1) % sock->rx_buf_size;
                sock->rx_buf[sock->rx_write_ptr] = (uint8_t)((src_port >> 8) & 0xFF);
                sock->rx_write_ptr = (sock->rx_write_ptr + 1) % sock->rx_buf_size;

                sock->rx_buf[sock->rx_write_ptr] = (uint8_t)(payload_len & 0xFF);
                sock->rx_write_ptr = (sock->rx_write_ptr + 1) % sock->rx_buf_size;
                sock->rx_buf[sock->rx_write_ptr] = (uint8_t)((payload_len >> 8) & 0xFF);
                sock->rx_write_ptr = (sock->rx_write_ptr + 1) % sock->rx_buf_size;

                // Write payload
                for (uint32_t k = 0; k < payload_len; k++) {
                    sock->rx_buf[sock->rx_write_ptr] = payload[k];
                    sock->rx_write_ptr = (sock->rx_write_ptr + 1) % sock->rx_buf_size;
                }

                // Wake up readers
                wait_queue_wake_all(&sock->wait_q);
                io_event_notify();
            }
            break;
        }
    }
}

void net_udp_send(struct socket *sock, const uint8_t *dest_ip, uint16_t dest_port, const void *payload, uint16_t len)
{
    static uint8_t udp_buffer[1600];
    if (sizeof(struct udp_hdr) + len > sizeof(udp_buffer)) return;

    struct udp_hdr *udp = (struct udp_hdr *)udp_buffer;
    udp->src_port = (uint16_t)((sock->local_port >> 8) | (sock->local_port << 8));
    udp->dest_port = (uint16_t)((dest_port >> 8) | (dest_port << 8));
    udp->len = (uint16_t)(((sizeof(struct udp_hdr) + len) >> 8) | ((sizeof(struct udp_hdr) + len) << 8));
    udp->csum = 0; // 0 means ignored in IPv4

    memcpy(udp_buffer + sizeof(struct udp_hdr), payload, len);

    net_ipv4_send(dest_ip, IP_PROTO_UDP, udp_buffer, sizeof(struct udp_hdr) + len);
}

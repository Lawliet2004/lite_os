#include <net/net.h>
#include <lib/string.h>
#include <mm/heap.h>

extern struct socket socket_table[];
extern void net_ipv4_send(const uint8_t dest_ip[4], uint8_t proto, const void *payload, uint16_t len);
extern uint8_t my_ip[4];

static inline uint16_t swap16(uint16_t v) { return (v << 8) | (v >> 8); }
static inline uint32_t swap32(uint32_t v) {
    return ((v & 0xFF000000) >> 24) |
           ((v & 0x00FF0000) >> 8)  |
           ((v & 0x0000FF00) << 8)  |
           ((v & 0x000000FF) << 24);
}

struct tcp_pseudo_hdr {
    uint8_t src_ip[4];
    uint8_t dest_ip[4];
    uint8_t reserved;
    uint8_t proto;
    uint16_t tcp_len;
} __attribute__((packed));

void net_tcp_send_segment(struct socket *sock, uint8_t flags, const void *payload, uint16_t len)
{
    static uint8_t tcp_packet[1600];
    uint16_t tcp_len = sizeof(struct tcp_hdr) + len;
    if (tcp_len > sizeof(tcp_packet)) return;

    struct tcp_hdr *tcp = (struct tcp_hdr *)tcp_packet;
    tcp->src_port = swap16(sock->local_port);
    tcp->dest_port = swap16(sock->remote_port);
    tcp->seq = swap32(sock->snd_nxt);
    tcp->ack = swap32(sock->rcv_nxt);
    tcp->data_offset = (uint8_t)(5 << 4); // 20 bytes header, no options
    tcp->flags = flags;
    tcp->window = swap16(4096);
    tcp->csum = 0;
    tcp->urgent = 0;

    if (len > 0 && payload != 0) {
        memcpy(tcp_packet + sizeof(struct tcp_hdr), payload, len);
    }

    // Compute TCP Checksum with pseudo-header
    struct tcp_pseudo_hdr phdr;
    memcpy(phdr.src_ip, my_ip, 4);
    memcpy(phdr.dest_ip, sock->remote_ip, 4);
    phdr.reserved = 0;
    phdr.proto = IP_PROTO_TCP;
    phdr.tcp_len = swap16(tcp_len);

    static uint8_t checksum_buffer[2000];
    memcpy(checksum_buffer, &phdr, sizeof(phdr));
    memcpy(checksum_buffer + sizeof(phdr), tcp_packet, tcp_len);

    tcp->csum = ip_checksum(checksum_buffer, sizeof(phdr) + tcp_len);

    net_ipv4_send(sock->remote_ip, IP_PROTO_TCP, tcp_packet, tcp_len);

    // Update seq for SYN, FIN, and payload
    if (flags & (TCP_FLAG_SYN | TCP_FLAG_FIN)) {
        sock->snd_nxt++;
    }
    sock->snd_nxt += len;
}

void net_tcp_receive(const uint8_t *src_ip, const void *data, uint16_t len)
{
    if (len < sizeof(struct tcp_hdr)) return;

    const struct tcp_hdr *tcp = (const struct tcp_hdr *)data;
    uint16_t src_port = swap16(tcp->src_port);
    uint16_t dest_port = swap16(tcp->dest_port);
    uint32_t seq = swap32(tcp->seq);
    uint32_t ack = swap32(tcp->ack);
    uint8_t flags = tcp->flags;

    uint8_t header_len = (uint8_t)((tcp->data_offset >> 4) * 4);
    if (len < header_len) return;

    const uint8_t *payload = (const uint8_t *)data + header_len;
    uint16_t payload_len = len - header_len;

    struct socket *sock = 0;
    struct socket *listener = 0;

    // Find matching socket
    for (int i = 0; i < 64; i++) {
        if (socket_table[i].valid && socket_table[i].type == SOCKET_TYPE_TCP) {
            if (socket_table[i].local_port == dest_port) {
                if (socket_table[i].tcp_state == TCP_STATE_LISTEN) {
                    listener = &socket_table[i];
                } else if (socket_table[i].remote_port == src_port &&
                           memcmp(socket_table[i].remote_ip, src_ip, 4) == 0) {
                    sock = &socket_table[i];
                    break;
                }
            }
        }
    }

    if (sock == 0 && listener != 0) {
        // Listening socket receives SYN -> create child socket
        if (flags & TCP_FLAG_SYN) {
            // Check backlog
            if (listener->accept_count < 8) {
                // Find empty slot for child
                for (int i = 0; i < 64; i++) {
                    if (!socket_table[i].valid) {
                        struct socket *child = &socket_table[i];
                        child->valid = true;
                        child->type = SOCKET_TYPE_TCP;
                        child->local_port = listener->local_port;
                        child->remote_port = src_port;
                        memcpy(child->remote_ip, src_ip, 4);
                        child->rx_buf_size = 8192;
                        child->rx_buf = kmalloc(child->rx_buf_size);
                        child->rx_read_ptr = 0;
                        child->rx_write_ptr = 0;
                        child->tcp_state = TCP_STATE_SYN_RECEIVED;
                        child->snd_nxt = 1000; // random ISN
                        child->rcv_nxt = seq + 1;
                        child->snd_una = child->snd_nxt;
                        child->parent_listener = listener;
                        child->bound = true;
                        child->connected = false;
                        child->closed = false;
                        wait_queue_init(&child->wait_q);

                        // Send SYN-ACK
                        net_tcp_send_segment(child, TCP_FLAG_SYN | TCP_FLAG_ACK, 0, 0);
                        break;
                    }
                }
            }
        }
        return;
    }

    if (sock == 0) return;

    // TCP State Machine
    if (sock->tcp_state == TCP_STATE_SYN_SENT) {
        if ((flags & (TCP_FLAG_SYN | TCP_FLAG_ACK)) == (TCP_FLAG_SYN | TCP_FLAG_ACK)) {
            sock->rcv_nxt = seq + 1;
            sock->snd_una = ack;
            sock->tcp_state = TCP_STATE_ESTABLISHED;
            sock->connected = true;

            // Send ACK
            net_tcp_send_segment(sock, TCP_FLAG_ACK, 0, 0);

            // Wake up waiters
            wait_queue_wake_all(&sock->wait_q);
        }
    } else if (sock->tcp_state == TCP_STATE_SYN_RECEIVED) {
        if (flags & TCP_FLAG_ACK) {
            sock->snd_una = ack;
            sock->tcp_state = TCP_STATE_ESTABLISHED;
            sock->connected = true;

            // Put in parent backlog
            struct socket *parent = sock->parent_listener;
            if (parent != 0 && parent->accept_count < 8) {
                parent->accept_queue[parent->accept_count++] = sock;
                wait_queue_wake_all(&parent->wait_q);
            }
        }
    } else if (sock->tcp_state == TCP_STATE_ESTABLISHED) {
        if (flags & TCP_FLAG_ACK) {
            sock->snd_una = ack;
        }

        if (payload_len > 0) {
            if (seq == sock->rcv_nxt) {
                // Append payload to read buffer
                uint32_t free_space;
                uint32_t w = sock->rx_write_ptr;
                uint32_t r = sock->rx_read_ptr;
                if (w >= r) {
                    free_space = sock->rx_buf_size - (w - r);
                } else {
                    free_space = r - w;
                }

                if (free_space > payload_len + 1) {
                    for (uint32_t k = 0; k < payload_len; k++) {
                        sock->rx_buf[sock->rx_write_ptr] = payload[k];
                        sock->rx_write_ptr = (sock->rx_write_ptr + 1) % sock->rx_buf_size;
                    }
                    sock->rcv_nxt += payload_len;

                    // Send ACK
                    net_tcp_send_segment(sock, TCP_FLAG_ACK, 0, 0);

                    // Wake up readers
                    wait_queue_wake_all(&sock->wait_q);
                }
            } else if (seq < sock->rcv_nxt) {
                // Duplicate data -> just ACK the current rcv_nxt
                net_tcp_send_segment(sock, TCP_FLAG_ACK, 0, 0);
            }
        }

        if (flags & TCP_FLAG_FIN) {
            sock->rcv_nxt++;
            net_tcp_send_segment(sock, TCP_FLAG_ACK, 0, 0);
            sock->tcp_state = TCP_STATE_CLOSE_WAIT;
            // Wake up readers for EOF
            wait_queue_wake_all(&sock->wait_q);
        }
    } else if (sock->tcp_state == TCP_STATE_FIN_WAIT_1) {
        if (flags & TCP_FLAG_ACK) {
            sock->snd_una = ack;
            sock->tcp_state = TCP_STATE_FIN_WAIT_2;
        }
        if (flags & TCP_FLAG_FIN) {
            sock->rcv_nxt++;
            net_tcp_send_segment(sock, TCP_FLAG_ACK, 0, 0);
            sock->tcp_state = TCP_STATE_CLOSED;
            sock->closed = true;
            wait_queue_wake_all(&sock->wait_q);
        }
    } else if (sock->tcp_state == TCP_STATE_FIN_WAIT_2) {
        if (flags & TCP_FLAG_FIN) {
            sock->rcv_nxt++;
            net_tcp_send_segment(sock, TCP_FLAG_ACK, 0, 0);
            sock->tcp_state = TCP_STATE_CLOSED;
            sock->closed = true;
            wait_queue_wake_all(&sock->wait_q);
        }
    } else if (sock->tcp_state == TCP_STATE_LAST_ACK) {
        if (flags & TCP_FLAG_ACK) {
            sock->snd_una = ack;
            sock->tcp_state = TCP_STATE_CLOSED;
            sock->closed = true;
            wait_queue_wake_all(&sock->wait_q);
        }
    }
}

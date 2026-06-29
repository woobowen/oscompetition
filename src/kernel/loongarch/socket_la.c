/*
 * socket_la.c — minimal AF_INET loopback socket layer implementation.
 *
 * TCP (SOCK_STREAM):
 *   socket()  → allocate a socket slot
 *   bind()    → assign local address / port (auto-port for port 0)
 *   listen()  → mark as passive, set backlog
 *   connect() → find listener by (addr, port), create connected pair
 *   accept()  → dequeue pre-connected socket from listener's backlog
 *   send()    → copy data into peer's circular receive buffer
 *   recv()    → drain data from own receive buffer
 *   close()   → notify peer (EOF), free socket resources
 *
 * UDP (SOCK_DGRAM):
 *   socket()  → allocate a socket slot (same pool)
 *   bind()    → assign local address / port
 *   sendto()  → copy datagram to destination socket's queue
 *   recvfrom()→ dequeue oldest datagram, return with source address
 *
 * RAW (SOCK_RAW):
 *   socket()/bind()/poll()/sendto() are accepted as a minimal compatibility
 *   surface for protocol self-tests.  No packets leave this in-kernel
 *   loopback model; sends report the copied byte count.
 *
 * Loopback design:
 *   No real network stack.  All addresses are 127.0.0.1 (or 0.0.0.0
 *   for bind-any).  connect() finds the listening socket and sets up
 *   a peer pair instantly — no explicit SYN handshake.  Flow control
 *   is achieved via blocking when the receive buffer is full / empty.
 */

#include "early_boot.h"
#include "socket_la.h"
#include "proc.h"

#define LA_SOCKET_DBG 0
#if LA_SOCKET_DBG
static void la_sock_dbg(const char *tag, uint64_t a, uint64_t b, uint64_t c)
{
    static int count = 0;
    struct la_proc *p = la_current_proc();

    if (count++ >= 360)
        return;
    la_uart_puts("  [sock] pid=");
    la_uart_put_hex(p ? (uint64_t)p->pid : 0);
    la_uart_puts(" ");
    la_uart_puts(tag);
    la_uart_puts(" a=");
    la_uart_put_hex(a);
    la_uart_puts(" b=");
    la_uart_put_hex(b);
    la_uart_puts(" c=");
    la_uart_put_hex(c);
    la_uart_puts("\n");
}
#else
static void la_sock_dbg(const char *tag, uint64_t a, uint64_t b, uint64_t c)
{
    (void)tag; (void)a; (void)b; (void)c;
}
#endif

/* ---- Static socket pool ---- */
static struct la_socket la_sockets[LA_NSOCK];

/* ---- Helpers ---- */

static void la_sock_release_slot(struct la_socket *s)
{
    if (!s)
        return;
    if (s->recv_buf) {
        la_pmem_free(s->recv_buf);
        s->recv_buf = 0;
    }
    s->used  = 0;
    s->refs  = 0;
    s->domain = 0;
    s->type  = 0;
    s->protocol = 0;
    s->raw_checksum_offset = -1;
    s->ipv6_recvpktinfo = 0;
    s->ipv6_recvopts = 0;
    for (int i = 0; i < 8; i++)
        s->icmp6_filter[i] = 0;
    s->state = LA_SOCK_CLOSED;
    s->peer  = 0;
    s->unix_bound = 0;
    s->unix_path[0] = '\0';
}

static int la_sock_streq(const char *a, const char *b)
{
    int i = 0;
    if (!a || !b)
        return 0;
    while (a[i] && b[i]) {
        if (a[i] != b[i])
            return 0;
        i++;
    }
    return a[i] == '\0' && b[i] == '\0';
}

static void la_sock_copy_path(char *dst, const char *src)
{
    int i = 0;
    while (src[i] && i < 255) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

/* Convert uint16 from network byte order (big-endian) to host (little-endian).
 * LoongArch is little-endian, so we flip the bytes. */
static uint16_t ntohs(uint16_t n) {
    return (uint16_t)((n >> 8) | (n << 8));
}
static uint16_t htons(uint16_t h) {
    return ntohs(h);
}

/* ---- Initialisation ---- */
void la_socket_init(void)
{
    for (int i = 0; i < LA_NSOCK; i++) {
        la_sockets[i].used  = 0;
        la_sockets[i].state = LA_SOCK_CLOSED;
        la_sockets[i].recv_buf = 0;
    }
    la_uart_puts("  socket: pool initialized (");
    la_uart_put_hex(LA_NSOCK);
    la_uart_puts(" sockets)\n");
}

/* Allocate a free socket slot.  Returns index or -1. */
int la_sock_alloc(void)
{
    for (int i = 0; i < LA_NSOCK; i++) {
        if (!la_sockets[i].used) {
            struct la_socket *s = &la_sockets[i];
            char *recv_buf = (char *)la_pmem_alloc();
            if (!recv_buf)
                return -1;
            s->used  = 1;
            s->refs  = 1;
            s->domain = 0;
            s->type  = 0;
            s->protocol = 0;
            s->raw_checksum_offset = -1;
            s->ipv6_recvpktinfo = 0;
            s->ipv6_recvopts = 0;
            for (int j = 0; j < 8; j++)
                s->icmp6_filter[j] = 0;
            s->state = LA_SOCK_CLOSED;
            s->laddr = 0;
            s->lport = 0;
            s->raddr = 0;
            s->rport = 0;
            s->peer  = 0;
            s->recv_buf   = recv_buf;
            s->recv_head  = 0;
            s->recv_tail  = 0;
            s->recv_total = 0;
            s->recv_eof   = 0;
            s->backlog    = 0;
            s->accept_cnt = 0;
            for (int j = 0; j < LA_TCP_ACCEPT_BACKLOG; j++)
                s->accept_q[j] = 0;
            s->dgram_head = 0;
            s->dgram_cnt  = 0;
            s->mcast_joined = 0;
            s->unix_bound = 0;
            s->unix_path[0] = '\0';
            for (int j = 0; j < LA_UDP_DGRAM_QUEUE; j++)
                s->dgram_q[j].len = 0;
            s->waiting_recv   = 0;
            s->waiting_send   = 0;
            s->waiting_accept = 0;
            return i;
        }
    }
    return -1;
}

/* Find a TCP listener socket by (addr, port).  addr=0 means "any address". */
static int la_sock_find_listener(uint32_t addr, uint16_t port)
{
    for (int i = 0; i < LA_NSOCK; i++) {
        struct la_socket *s = &la_sockets[i];
        if (!s->used)                 continue;
        if (s->type != LA_SOCK_STREAM) continue;
        if (s->state != LA_SOCK_LISTEN) continue;
        if (s->lport != port)         continue;
        /* addr 0 = INADDR_ANY, matches any client */
        if (s->laddr != 0 && s->laddr != addr) continue;
        return i;
    }
    return -1;
}

/* Find a UDP socket by destination (addr, port) and source tuple.
 * Connected UDP sockets only receive datagrams from their connected peer;
 * new flows fall back to an unconnected socket bound to the destination port. */
static int la_sock_find_udp(uint32_t addr, uint16_t port,
                            uint32_t src_addr, uint16_t src_port)
{
    int unconnected = -1;

    for (int i = 0; i < LA_NSOCK; i++) {
        struct la_socket *s = &la_sockets[i];
        if (!s->used)                continue;
        if (s->type != LA_SOCK_DGRAM) continue;
        if (s->lport != port)        continue;
        if (s->laddr != 0 && s->laddr != addr) continue;
        if (s->rport != 0) {
            if (s->rport == src_port &&
                (s->raddr == 0 || s->raddr == src_addr))
                return i;
            continue;
        }
        if (unconnected < 0)
            unconnected = i;
    }
    return unconnected;
}

static int la_sock_interrupted_by_signal(void)
{
    struct la_proc *p = la_current_proc();
    if (!p || !p->is_user)
        return 0;

    uint64_t pending = p->sig_pending & ~p->sig_mask;
    pending &= ~(1UL << LA_SIGCHLD);
    pending |= (p->sig_pending & (1UL << LA_SIGKILL));
    pending |= (p->sig_pending & (1UL << LA_SIGSTOP));
    return pending != 0;
}

/* Auto-assign a port starting from LA_AUTO_PORT_BASE.
 * Returns 0 if all ports are taken (unlikely with 64K range). */
static uint16_t la_sock_auto_port(void)
{
    static uint16_t next_port = LA_AUTO_PORT_BASE;
    uint16_t start = next_port;
    for (;;) {
        int conflict = 0;
        for (int i = 0; i < LA_NSOCK; i++) {
            if (la_sockets[i].used && la_sockets[i].lport == htons(next_port)) {
                conflict = 1;
                break;
            }
        }
        if (!conflict) {
            uint16_t assigned = htons(next_port);
            next_port++;
            if (next_port == 0) next_port = LA_AUTO_PORT_BASE;
            return assigned;
        }
        next_port++;
        if (next_port == 0) next_port = LA_AUTO_PORT_BASE;
        if (next_port == start) return 0;  /* exhausted */
    }
}

/* ================================================================
 *  Public API
 * ================================================================ */

/* socket(domain, type, protocol) → socket index */
int la_sock_socket(int domain, int type, int protocol)
{
    int stored_type = type;

    if (domain != LA_AF_UNIX && domain != LA_AF_INET &&
        domain != LA_AF_INET6 && domain != LA_AF_NETLINK &&
        domain != LA_AF_PACKET)
        return -1;
    if (type == LA_SOCK_SEQPACKET)
        stored_type = LA_SOCK_STREAM;
    if (stored_type != LA_SOCK_STREAM && stored_type != LA_SOCK_DGRAM &&
        stored_type != LA_SOCK_RAW)
        return -1;
    if (stored_type == LA_SOCK_RAW &&
        domain != LA_AF_INET && domain != LA_AF_INET6 &&
        domain != LA_AF_NETLINK && domain != LA_AF_PACKET)
        return -1;
    if (domain == LA_AF_NETLINK && protocol != 0)
        return -1;
    if (domain == LA_AF_NETLINK && stored_type != LA_SOCK_RAW &&
        stored_type != LA_SOCK_DGRAM)
        return -1;
    if (domain == LA_AF_PACKET && stored_type != LA_SOCK_RAW &&
        stored_type != LA_SOCK_DGRAM)
        return -1;

    int idx = la_sock_alloc();
    if (idx < 0) return -1;

    la_sockets[idx].domain = domain;
    la_sockets[idx].type = stored_type;
    la_sockets[idx].protocol = protocol;
    la_sockets[idx].raw_checksum_offset = -1;
    la_sockets[idx].ipv6_recvpktinfo = 0;
    la_sockets[idx].ipv6_recvopts = 0;
    for (int i = 0; i < 8; i++)
        la_sockets[idx].icmp6_filter[i] = 0;
    return idx;
}

static int la_sock_packet_enqueue_arp_reply(struct la_socket *s,
                                            const uint8_t *req, uint32_t len)
{
    if (!s || !req || s->dgram_cnt >= LA_UDP_DGRAM_QUEUE)
        return -1;
    uint32_t off = 0;
    if (len >= 42 && req[12] == 0x08 && req[13] == 0x06)
        off = 14;  /* Ethernet frame carrying ARP. */
    if (len < off + 28)
        return -1;
    if (req[off + 0] != 0x00 || req[off + 1] != 0x01 ||
        req[off + 2] != 0x08 || req[off + 3] != 0x00 ||
        req[off + 4] != 0x06 || req[off + 5] != 0x04)
        return -1;

    int qi = (s->dgram_head + s->dgram_cnt) % LA_UDP_DGRAM_QUEUE;
    struct la_udp_dgram *dg = &s->dgram_q[qi];
    static const uint8_t reply_mac[6] = { 0x02, 0x00, 0x00, 0x00, 0x00, 0x02 };

    if (off == 14) {
        for (int i = 0; i < 6; i++)
            dg->data[i] = req[6 + i];       /* dst = requester */
        for (int i = 0; i < 6; i++)
            dg->data[6 + i] = reply_mac[i]; /* src = synthetic peer */
        dg->data[12] = 0x08;
        dg->data[13] = 0x06;
    }
    dg->data[off + 0] = 0x00; dg->data[off + 1] = 0x01; /* Ethernet */
    dg->data[off + 2] = 0x08; dg->data[off + 3] = 0x00; /* IPv4 */
    dg->data[off + 4] = 0x06; dg->data[off + 5] = 0x04;
    dg->data[off + 6] = 0x00; dg->data[off + 7] = 0x02; /* ARP reply */
    for (int i = 0; i < 6; i++)
        dg->data[off + 8 + i] = reply_mac[i];
    for (int i = 0; i < 4; i++)
        dg->data[off + 14 + i] = req[off + 24 + i]; /* sender IP = requested target */
    for (int i = 0; i < 6; i++)
        dg->data[off + 18 + i] = req[off + 8 + i];  /* target MAC = requester */
    for (int i = 0; i < 4; i++)
        dg->data[off + 24 + i] = req[off + 14 + i]; /* target IP = requester */

    dg->len = (uint16_t)(off + 28);
    dg->src_addr = 0;
    dg->src_port = 0;
    s->dgram_cnt++;

    if (s->waiting_recv) {
        s->waiting_recv = 0;
        la_proc_wakeup_chan(&s->waiting_recv);
    }
    return 0;
}

static int la_sock_raw_send_result(struct la_socket *s, uint32_t len)
{
    if (!s || s->type != LA_SOCK_RAW)
        return -1;
    if (s->raw_checksum_offset >= 0) {
        uint32_t off = (uint32_t)s->raw_checksum_offset;
        if (off > len || len - off < 2)
            return -2;  /* EINVAL: checksum field is outside payload */
    }
    return (int)len;
}

static int la_sock_raw_filter_allows(struct la_socket *dst,
                                     const uint8_t *data, uint32_t len)
{
    if (!dst || dst->protocol != 58 || len == 0)
        return 1;

    uint32_t type = data[0];
    uint32_t word = type >> 5;
    uint32_t bit = type & 31U;
    if (word >= 8)
        return 1;
    return (dst->icmp6_filter[word] & (1U << bit)) == 0;
}

static int la_sock_raw_enqueue(struct la_socket *dst, const void *buf,
                               uint32_t len, uint32_t src_addr)
{
    if (!dst || dst->dgram_cnt >= LA_UDP_DGRAM_QUEUE)
        return -1;
    if (len > LA_UDP_DGRAM_MAX)
        len = LA_UDP_DGRAM_MAX;

    int qi = (dst->dgram_head + dst->dgram_cnt) % LA_UDP_DGRAM_QUEUE;
    struct la_udp_dgram *dg = &dst->dgram_q[qi];
    const uint8_t *src = (const uint8_t *)buf;
    for (uint32_t i = 0; i < len; i++)
        dg->data[i] = src[i];
    dg->len = (uint16_t)len;
    dg->src_addr = src_addr;
    dg->src_port = 0;
    dst->dgram_cnt++;

    if (dst->waiting_recv) {
        dst->waiting_recv = 0;
        la_proc_wakeup_chan(&dst->waiting_recv);
    }
    return 0;
}

/* bind(idx, addr, port).  port=0 → auto-assign. */
int la_sock_bind(int idx, uint32_t addr, uint16_t port)
{
    if (idx < 0 || idx >= LA_NSOCK || !la_sockets[idx].used) return -1;

    struct la_socket *s = &la_sockets[idx];

    if (port == 0) {
        port = la_sock_auto_port();
        if (port == 0) return -1;
    }

    s->laddr = addr;
    s->lport = port;
    return 0;
}

/* Minimal AF_UNIX pathname bind.
 * Returns 0 on success, -1 for invalid input, -2 for rebind of the same
 * socket, and -3 when another live socket already owns the pathname. */
int la_sock_bind_unix(int idx, const char *path)
{
    if (idx < 0 || idx >= LA_NSOCK || !la_sockets[idx].used ||
        !path || path[0] == '\0')
        return -1;

    struct la_socket *s = &la_sockets[idx];
    if (s->domain != LA_AF_UNIX)
        return -1;
    if (s->unix_bound)
        return -2;

    for (int i = 0; i < LA_NSOCK; i++) {
        if (i == idx || !la_sockets[i].used || !la_sockets[i].unix_bound)
            continue;
        if (la_sock_streq(la_sockets[i].unix_path, path))
            return -3;
    }

    la_sock_copy_path(s->unix_path, path);
    s->unix_bound = 1;
    s->lport = 1;
    return 0;
}

int la_sock_unix_bound(int idx)
{
    if (idx < 0 || idx >= LA_NSOCK || !la_sockets[idx].used)
        return 0;
    return la_sockets[idx].unix_bound;
}

/* listen(idx, backlog) */
int la_sock_listen(int idx, int backlog)
{
    if (idx < 0 || idx >= LA_NSOCK || !la_sockets[idx].used) return -1;

    struct la_socket *s = &la_sockets[idx];
    if (s->type != LA_SOCK_STREAM) return -1;
    if (s->lport == 0) return -1;   /* must bind first */

    s->state   = LA_SOCK_LISTEN;
    s->backlog = backlog;
    if (s->backlog > LA_TCP_ACCEPT_BACKLOG)
        s->backlog = LA_TCP_ACCEPT_BACKLOG;
    return 0;
}

/* connect(idx, addr, port) — establish TCP connection to a listener.
 * Finds the matching listening socket, creates a connected child from
 * the pool, pairs them, and enqueues the child on the listener's accept_q. */
int la_sock_connect(int idx, uint32_t addr, uint16_t port)
{
    if (idx < 0 || idx >= LA_NSOCK || !la_sockets[idx].used) return -1;

    struct la_socket *client = &la_sockets[idx];
    if (client->type == LA_SOCK_DGRAM || client->type == LA_SOCK_RAW) {
        client->raddr = addr;
        client->rport = port;
        if (client->lport == 0) {
            client->laddr = (addr == 0x0100000A) ? 0x0200000A : 0x0100007F;
            client->lport = la_sock_auto_port();
        }
        client->state = LA_SOCK_ESTABLISHED;
        return 0;
    }

    if (client->type != LA_SOCK_STREAM) return -1;

    /* Find listening socket */
    int listener_idx = la_sock_find_listener(addr, port);
    if (listener_idx < 0) return -1;

    struct la_socket *listener = &la_sockets[listener_idx];

    /* Check backlog */
    if (listener->accept_cnt >= listener->backlog) return -1;

    /* Allocate a new socket for the server side of the connection */
    int child_idx = la_sock_alloc();
    if (child_idx < 0) return -1;

    struct la_socket *child = &la_sockets[child_idx];
    child->type  = LA_SOCK_STREAM;
    child->state = LA_SOCK_ESTABLISHED;
    child->laddr = listener->laddr;
    child->lport = listener->lport;
    child->raddr = addr;
    child->rport = port;

    /* Set up the connected pair */
    client->state = LA_SOCK_ESTABLISHED;
    client->peer  = child;
    client->raddr = addr;
    client->rport = port;
    /* laddr / lport set by bind(), or auto-assign ephemeral */
    if (client->lport == 0) {
        client->laddr = 0x0100007F;  /* 127.0.0.1 */
        client->lport = la_sock_auto_port();
    }

    child->peer = client;

    /* Enqueue on listener's accept_q */
    listener->accept_q[listener->accept_cnt++] = child;

    /* Wake the listener if it's blocked in accept() */
    if (listener->waiting_accept) {
        listener->waiting_accept = 0;
        la_proc_wakeup_chan(&listener->accept_cnt);
    }

    return 0;
}

/* accept(idx, &addr, &port) — dequeue a pending connection.
 * Blocks if the accept queue is empty (cooperative sleep). */
int la_sock_accept(int idx, uint32_t *uaddr, uint16_t *uport)
{
    if (idx < 0 || idx >= LA_NSOCK || !la_sockets[idx].used) return -1;

    struct la_socket *listener = &la_sockets[idx];
    if (listener->type != LA_SOCK_STREAM) return -1;
    if (listener->state != LA_SOCK_LISTEN) return -1;

    /* Block while queue is empty (cooperative scheduler — no lost wakeup) */
    while (listener->accept_cnt == 0) {
        listener->waiting_accept = 1;
        la_proc_sleep_chan(&listener->accept_cnt);
        if (la_sock_interrupted_by_signal()) {
            listener->waiting_accept = 0;
            return -2;
        }
    }

    /* Dequeue the oldest pending connection (FIFO) */
    struct la_socket *child = listener->accept_q[0];
    listener->accept_cnt--;
    for (int i = 0; i < listener->accept_cnt; i++)
        listener->accept_q[i] = listener->accept_q[i + 1];
    listener->accept_q[listener->accept_cnt] = 0;

    /* Return remote address to caller if pointers provided */
    if (uaddr) *uaddr = child->raddr;
    if (uport) *uport = child->rport;

    /* Return the child's socket index (caller maps it to an fd) */
    return (int)(child - la_sockets);
}

/* send(idx, buf, len) — TCP send.
 * Copies data directly into the peer's receive buffer.
 * Blocks if the peer's buffer is full. */
int la_sock_send(int idx, const void *buf, uint32_t len)
{
    if (idx < 0 || idx >= LA_NSOCK || !la_sockets[idx].used) return -1;

    struct la_socket *s = &la_sockets[idx];
    if (s->domain == LA_AF_PACKET) {
        la_sock_packet_enqueue_arp_reply(s, (const uint8_t *)buf, len);
        return (int)len;
    }
    if (s->type == LA_SOCK_DGRAM) {
        if (s->rport == 0) return -1;
        return la_sock_sendto(idx, buf, len, s->raddr, s->rport);
    }
    if (s->type == LA_SOCK_RAW)
        return la_sock_raw_send_result(s, len);

    if (s->type != LA_SOCK_STREAM) return -1;
    if (s->state != LA_SOCK_ESTABLISHED) return -1;

    struct la_socket *peer = s->peer;
    if (!peer || peer->state == LA_SOCK_CLOSED)
        return -1;  /* broken pipe / connection reset */

    const uint8_t *src = (const uint8_t *)buf;
    uint32_t done = 0;

    while (done < len) {
        /* If peer is closed, stop and return what we've written (or -1) */
        if (peer->state == LA_SOCK_CLOSED)
            return done > 0 ? (int)done : -1;

        /* If peer's receive buffer is full, block until the peer reads */
        while (peer->recv_total >= LA_SOCK_RECV_BUF && peer->state != LA_SOCK_CLOSED) {
            s->waiting_send = 1;
            la_proc_sleep_chan(&s->waiting_send);
        }

        /* How much space is left? */
        uint32_t space = LA_SOCK_RECV_BUF - peer->recv_total;
        uint32_t chunk = len - done;
        if (chunk > space) chunk = space;

        /* Copy into peer's circular receive buffer */
        uint32_t wi = peer->recv_tail % LA_SOCK_RECV_BUF;
        if (wi + chunk > LA_SOCK_RECV_BUF) {
            uint32_t first = LA_SOCK_RECV_BUF - wi;
            for (uint32_t j = 0; j < first; j++)
                peer->recv_buf[wi + j] = src[done + j];
            for (uint32_t j = 0; j < chunk - first; j++)
                peer->recv_buf[j] = src[done + first + j];
        } else {
            for (uint32_t j = 0; j < chunk; j++)
                peer->recv_buf[wi + j] = src[done + j];
        }
        peer->recv_tail   += chunk;
        peer->recv_total  += chunk;
        done += chunk;

        /* Wake the peer if it's blocked in recv() */
        if (peer->waiting_recv) {
            peer->waiting_recv = 0;
            la_proc_wakeup_chan(&peer->waiting_recv);
        }
    }

    return (int)done;
}

/* recv(idx, buf, len) — TCP receive.
 * Drains data from own receive buffer.
 * Blocks if buffer is empty and remote hasn't closed.
 * Returns 0 on EOF (remote closed and buffer drained). */
int la_sock_recv(int idx, void *buf, uint32_t len)
{
    if (idx < 0 || idx >= LA_NSOCK || !la_sockets[idx].used) return -1;

    struct la_socket *s = &la_sockets[idx];
    if (s->type == LA_SOCK_DGRAM)
        return la_sock_recvfrom(idx, buf, len, 0, 0);

    if (s->type != LA_SOCK_STREAM) return -1;
    if (s->state != LA_SOCK_ESTABLISHED) return -1;

    /* Block while buffer is empty and remote hasn't closed */
    while (s->recv_total == 0 && !s->recv_eof) {
        s->waiting_recv = 1;
        la_proc_sleep_chan(&s->waiting_recv);
    }

    if (s->recv_total == 0 && s->recv_eof)
        return 0;  /* EOF */

    uint32_t chunk = len;
    if (chunk > s->recv_total) chunk = s->recv_total;

    uint8_t *dst = (uint8_t *)buf;
    uint32_t ri = s->recv_head % LA_SOCK_RECV_BUF;
    uint32_t done = 0;

    while (done < chunk) {
        uint32_t seg = chunk - done;
        if (ri + seg > LA_SOCK_RECV_BUF)
            seg = LA_SOCK_RECV_BUF - ri;
        for (uint32_t j = 0; j < seg; j++)
            dst[done + j] = s->recv_buf[ri + j];
        done  += seg;
        ri    = (ri + seg) % LA_SOCK_RECV_BUF;
    }

    s->recv_head  += chunk;
    s->recv_total -= chunk;

        /* Wake the peer if it was blocked in send() (more buffer space now) */
        if (s->peer && s->peer->waiting_send) {
            s->peer->waiting_send = 0;
            la_proc_wakeup_chan(&s->peer->waiting_send);
        }

    return (int)chunk;
}

/* connect_pair(a, b) — cross-connect two sockets directly (no listen/accept).
 * Used by socketpair(AF_UNIX, SOCK_STREAM).  After this call, data written
 * to `a` is readable from `b` and vice versa. */
void la_sock_connect_pair(int a, int b)
{
    if (a < 0 || a >= LA_NSOCK || b < 0 || b >= LA_NSOCK) return;
    if (!la_sockets[a].used || !la_sockets[b].used) return;

    struct la_socket *sa = &la_sockets[a];
    struct la_socket *sb = &la_sockets[b];

    sa->type  = LA_SOCK_STREAM;
    sa->state = LA_SOCK_ESTABLISHED;
    sa->peer  = sb;
    sa->laddr = 0x0100007F;  /* 127.0.0.1 */
    sa->lport = 0;

    sb->type  = LA_SOCK_STREAM;
    sb->state = LA_SOCK_ESTABLISHED;
    sb->peer  = sa;
    sb->laddr = 0x0100007F;
    sb->lport = 0;
}

/* close(idx) — TCP close.
 * Sets peer's recv_eof, wakes blocked peer, and frees the slot once
 * both ends are closed (or after notifying). */
void la_sock_close(int idx)
{
    if (idx < 0 || idx >= LA_NSOCK || !la_sockets[idx].used) return;

    struct la_socket *s = &la_sockets[idx];
    if (s->refs > 1) {
        s->refs--;
        return;
    }

    struct la_socket *peer = s->peer;

    /* Mark peer's receive direction as closed */
    if (peer && peer->state == LA_SOCK_ESTABLISHED) {
        peer->recv_eof = 1;

        /* Wake peer if blocked in recv() */
        if (peer->waiting_recv) {
            peer->waiting_recv = 0;
            la_proc_wakeup_chan(&peer->waiting_recv);
        }
        /* Wake peer if blocked in send() (broken pipe) */
        if (peer->waiting_send) {
            peer->waiting_send = 0;
            la_proc_wakeup_chan(&peer->waiting_send);
        }

        /* Clear peer's back-pointer to us */
        peer->peer = 0;
    }

    /* If the peer is also closed (or nonexistent), free the peer's slot.
     * Otherwise leave it — the peer will free it when it closes. */
    if (peer && peer->state == LA_SOCK_CLOSED && peer->used) {
        la_sock_release_slot(peer);
    }

    /* Free this slot */
    la_sock_release_slot(s);
}

void la_sock_dup(int idx)
{
    if (idx < 0 || idx >= LA_NSOCK || !la_sockets[idx].used) return;
    la_sockets[idx].refs++;
}

int la_sock_type(int idx)
{
    if (idx < 0 || idx >= LA_NSOCK || !la_sockets[idx].used)
        return -1;
    return la_sockets[idx].type;
}

int la_sock_domain(int idx)
{
    if (idx < 0 || idx >= LA_NSOCK || !la_sockets[idx].used)
        return -1;
    return la_sockets[idx].domain;
}

int la_sock_set_ipv6_checksum(int idx, int offset)
{
    if (idx < 0 || idx >= LA_NSOCK || !la_sockets[idx].used)
        return -1;
    struct la_socket *s = &la_sockets[idx];
    if (s->type != LA_SOCK_RAW)
        return -1;
    if (offset < -1 || (offset >= 0 && (offset & 1)))
        return -1;
    s->raw_checksum_offset = offset;
    return 0;
}

int la_sock_set_ipv6_recvpktinfo(int idx, int enabled)
{
    if (idx < 0 || idx >= LA_NSOCK || !la_sockets[idx].used)
        return -1;
    la_sockets[idx].ipv6_recvpktinfo = enabled ? 1 : 0;
    if (enabled)
        la_sockets[idx].ipv6_recvopts |= LA_IPV6_RECVOPT_PKTINFO;
    else
        la_sockets[idx].ipv6_recvopts &= ~LA_IPV6_RECVOPT_PKTINFO;
    return 0;
}

int la_sock_get_ipv6_recvpktinfo(int idx, int *enabled)
{
    if (idx < 0 || idx >= LA_NSOCK || !la_sockets[idx].used || !enabled)
        return -1;
    *enabled = la_sockets[idx].ipv6_recvpktinfo;
    return 0;
}

int la_sock_set_ipv6_recvopt(int idx, uint32_t optbit, int enabled)
{
    if (idx < 0 || idx >= LA_NSOCK || !la_sockets[idx].used || optbit == 0)
        return -1;
    if (enabled)
        la_sockets[idx].ipv6_recvopts |= optbit;
    else
        la_sockets[idx].ipv6_recvopts &= ~optbit;
    if (optbit == LA_IPV6_RECVOPT_PKTINFO)
        la_sockets[idx].ipv6_recvpktinfo = enabled ? 1 : 0;
    return 0;
}

int la_sock_get_ipv6_recvopt(int idx, uint32_t optbit, int *enabled)
{
    if (idx < 0 || idx >= LA_NSOCK || !la_sockets[idx].used || !enabled ||
        optbit == 0)
        return -1;
    *enabled = (la_sockets[idx].ipv6_recvopts & optbit) ? 1 : 0;
    return 0;
}

uint32_t la_sock_get_ipv6_recvopts(int idx)
{
    if (idx < 0 || idx >= LA_NSOCK || !la_sockets[idx].used)
        return 0;
    return la_sockets[idx].ipv6_recvopts;
}

int la_sock_set_icmp6_filter(int idx, const uint32_t *filter_words)
{
    if (idx < 0 || idx >= LA_NSOCK || !la_sockets[idx].used || !filter_words)
        return -1;
    for (int i = 0; i < 8; i++)
        la_sockets[idx].icmp6_filter[i] = filter_words[i];
    return 0;
}

int la_sock_mcast_join(int idx)
{
    if (idx < 0 || idx >= LA_NSOCK || !la_sockets[idx].used)
        return -1;
    la_sockets[idx].mcast_joined = 1;
    return 0;
}

int la_sock_mcast_leave(int idx)
{
    if (idx < 0 || idx >= LA_NSOCK || !la_sockets[idx].used)
        return -1;
    if (!la_sockets[idx].mcast_joined)
        return -2;
    la_sockets[idx].mcast_joined = 0;
    return 0;
}

/* ---- UDP ---- */

/* sendto(idx, buf, len, addr, port) — UDP send.
 * Finds the destination socket by (addr, port) and queues a datagram.
 * Blocks if the destination's datagram queue is full. */
int la_sock_sendto(int idx, const void *buf, uint32_t len,
                   uint32_t addr, uint16_t port)
{
    if (idx < 0 || idx >= LA_NSOCK || !la_sockets[idx].used) return -1;

    struct la_socket *src = &la_sockets[idx];
    if (src->domain == LA_AF_PACKET) {
        la_sock_packet_enqueue_arp_reply(src, (const uint8_t *)buf, len);
        return (int)len;
    }
    if (src->type == LA_SOCK_RAW) {
        int raw_rc = la_sock_raw_send_result(src, len);
        if (raw_rc < 0)
            return raw_rc;

        uint32_t src_addr = src->laddr ? src->laddr : 0x0100007F;
        int delivered = 0;
        for (int i = 0; i < LA_NSOCK; i++) {
            struct la_socket *dst = &la_sockets[i];
            if (!dst->used || dst->type != LA_SOCK_RAW)
                continue;
            if (dst->protocol != src->protocol)
                continue;
            if (!la_sock_raw_filter_allows(dst, (const uint8_t *)buf, len))
                continue;
            if (la_sock_raw_enqueue(dst, buf, len, src_addr) == 0)
                delivered++;
        }
        (void)delivered;
        return raw_rc;
    }
    if (src->type != LA_SOCK_DGRAM) return -1;

    if (src->lport == 0) {
        src->laddr = 0x0100007F;
        src->lport = la_sock_auto_port();
        if (src->lport == 0) return -1;
    }
    uint32_t src_addr = src->laddr ? src->laddr : 0x0100007F;

    /* Find destination UDP socket */
    int dst_idx = la_sock_find_udp(addr, port, src_addr, src->lport);
    if (dst_idx < 0) {
        la_sock_dbg("udp_no_dst", (uint64_t)idx, (uint64_t)addr, (uint64_t)port);
        return -1;  /* no listener on this port */
    }

    struct la_socket *dst = &la_sockets[dst_idx];

    if (len > LA_UDP_DGRAM_MAX)
        len = LA_UDP_DGRAM_MAX;

    /* Block if queue is full */
    while (dst->dgram_cnt >= LA_UDP_DGRAM_QUEUE) {
        la_sock_dbg("udp_send_block", (uint64_t)idx, (uint64_t)dst_idx,
                    (uint64_t)dst->dgram_cnt);
        src->waiting_send = 1;
        la_proc_sleep_chan(&src->waiting_send);
    }

    /* Enqueue datagram */
    int qi = (dst->dgram_head + dst->dgram_cnt) % LA_UDP_DGRAM_QUEUE;
    struct la_udp_dgram *dg = &dst->dgram_q[qi];
    const uint8_t *src_data = (const uint8_t *)buf;
    for (uint32_t j = 0; j < len; j++)
        dg->data[j] = src_data[j];
    dg->len      = (uint16_t)len;
    dg->src_addr = src_addr;  /* 127.0.0.1 */
    dg->src_port = src->lport;
    dst->dgram_cnt++;
    la_sock_dbg("udp_send", (uint64_t)idx, (uint64_t)dst_idx, (uint64_t)len);

    /* Wake destination if blocked in recvfrom() */
    if (dst->waiting_recv) {
        dst->waiting_recv = 0;
        la_proc_wakeup_chan(&dst->waiting_recv);
    }

    return (int)len;
}

/* recvfrom(idx, buf, len, &addr, &port) — UDP receive.
 * Dequeues the oldest datagram and returns with source address.
 * Blocks if the queue is empty. */
int la_sock_recvfrom(int idx, void *buf, uint32_t len,
                     uint32_t *uaddr, uint16_t *uport)
{
    if (idx < 0 || idx >= LA_NSOCK || !la_sockets[idx].used) return -1;

    struct la_socket *s = &la_sockets[idx];
    if (s->domain == LA_AF_PACKET) {
        if (s->dgram_cnt == 0)
            return -1;
    }
    if (s->type != LA_SOCK_DGRAM && s->type != LA_SOCK_RAW) return -1;

    /* Block while queue is empty */
    while (s->dgram_cnt == 0) {
        la_sock_dbg("udp_recv_block", (uint64_t)idx, 0, 0);
        s->waiting_recv = 1;
        la_proc_sleep_chan(&s->waiting_recv);
    }

    /* Dequeue oldest datagram */
    struct la_udp_dgram *dg = &s->dgram_q[s->dgram_head];
    uint32_t dgram_len = dg->len;
    if (dgram_len > len) dgram_len = len;

    uint8_t *dst = (uint8_t *)buf;
    for (uint32_t j = 0; j < dgram_len; j++)
        dst[j] = dg->data[j];

    if (uaddr) *uaddr = dg->src_addr;
    if (uport) *uport = dg->src_port;

    /* Advance the queue */
    dg->len = 0;
    s->dgram_head = (s->dgram_head + 1) % LA_UDP_DGRAM_QUEUE;
    s->dgram_cnt--;

    /* Wake any sender blocked on queue-full */
    /* (We wake ALL — any sender will re-check its own waiting_send flag) */
    for (int i = 0; i < LA_NSOCK; i++) {
        if (la_sockets[i].used && la_sockets[i].waiting_send) {
            la_sockets[i].waiting_send = 0;
            la_proc_wakeup_chan(&la_sockets[i].waiting_send);
        }
    }

    la_sock_dbg("udp_recv", (uint64_t)idx, (uint64_t)dgram_len,
                (uint64_t)s->dgram_cnt);
    return (int)dgram_len;
}

int la_sock_readable(int idx)
{
    if (idx < 0 || idx >= LA_NSOCK || !la_sockets[idx].used) return -1;

    struct la_socket *s = &la_sockets[idx];
    if (s->domain == LA_AF_PACKET)
        return s->dgram_cnt > 0 ? 1 : 0;
    if (s->type == LA_SOCK_RAW)
        return s->dgram_cnt > 0 ? 1 : 0;
    if (s->type == LA_SOCK_DGRAM)
        return s->dgram_cnt > 0 ? 1 : 0;
    if (s->type != LA_SOCK_STREAM)
        return -1;
    if (s->state == LA_SOCK_LISTEN)
        return s->accept_cnt > 0 ? 1 : 0;
    if (s->state == LA_SOCK_ESTABLISHED)
        return (s->recv_total > 0 || s->recv_eof) ? 1 : 0;
    return -1;
}

int la_sock_writable(int idx)
{
    if (idx < 0 || idx >= LA_NSOCK || !la_sockets[idx].used) return -1;

    struct la_socket *s = &la_sockets[idx];
    if (s->domain == LA_AF_PACKET)
        return 1;
    if (s->type == LA_SOCK_RAW)
        return 1;
    if (s->type == LA_SOCK_DGRAM)
        return 1;
    if (s->type != LA_SOCK_STREAM)
        return -1;
    if (s->state != LA_SOCK_ESTABLISHED)
        return 0;
    if (!s->peer || s->peer->state == LA_SOCK_CLOSED)
        return 0;
    return s->peer->recv_total < LA_SOCK_RECV_BUF ? 1 : 0;
}

/* getname(idx, &addr, &port, peer) — getsockname (peer=0) or
 * getpeername (peer=1).  Returns local/remote address for connected
 * or bound sockets. */
int la_sock_getname(int idx, uint32_t *uaddr, uint16_t *uport, int peer)
{
    if (idx < 0 || idx >= LA_NSOCK || !la_sockets[idx].used) return -1;

    struct la_socket *s = &la_sockets[idx];

    if (peer) {
        /* getpeername: return remote address */
        if (s->state != LA_SOCK_ESTABLISHED) return -1;
        if (uaddr) *uaddr = s->raddr;
        if (uport) *uport = s->rport;
    } else {
        /* getsockname: return local address */
        if (uaddr) *uaddr = s->laddr;
        if (uport) *uport = s->lport;
    }
    return 0;
}

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
 * Loopback design:
 *   No real network stack.  All addresses are 127.0.0.1 (or 0.0.0.0
 *   for bind-any).  connect() finds the listening socket and sets up
 *   a peer pair instantly — no explicit SYN handshake.  Flow control
 *   is achieved via blocking when the receive buffer is full / empty.
 */

#include "early_boot.h"
#include "socket_la.h"
#include "proc.h"

/* ---- Static socket pool ---- */
static struct la_socket la_sockets[LA_NSOCK];

/* ---- Helpers ---- */

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
    }
    la_uart_puts("  socket: pool initialized (");
    la_uart_put_hex(LA_NSOCK);
    la_uart_puts(" sockets)\n");
}

/* Allocate a free socket slot.  Returns index or -1. */
static int la_sock_alloc(void)
{
    for (int i = 0; i < LA_NSOCK; i++) {
        if (!la_sockets[i].used) {
            struct la_socket *s = &la_sockets[i];
            s->used  = 1;
            s->type  = 0;
            s->state = LA_SOCK_CLOSED;
            s->laddr = 0;
            s->lport = 0;
            s->raddr = 0;
            s->rport = 0;
            s->peer  = 0;
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

/* Find a UDP socket by (addr, port) for sendto delivery. */
static int la_sock_find_udp(uint32_t addr, uint16_t port)
{
    for (int i = 0; i < LA_NSOCK; i++) {
        struct la_socket *s = &la_sockets[i];
        if (!s->used)                continue;
        if (s->type != LA_SOCK_DGRAM) continue;
        if (s->lport != port)        continue;
        if (s->laddr != 0 && s->laddr != addr) continue;
        return i;
    }
    return -1;
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
    (void)protocol;
    if (domain != LA_AF_INET) return -1;
    if (type != LA_SOCK_STREAM && type != LA_SOCK_DGRAM) return -1;

    int idx = la_sock_alloc();
    if (idx < 0) return -1;

    la_sockets[idx].type = type;
    return idx;
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

/* close(idx) — TCP close.
 * Sets peer's recv_eof, wakes blocked peer, and frees the slot once
 * both ends are closed (or after notifying). */
void la_sock_close(int idx)
{
    if (idx < 0 || idx >= LA_NSOCK || !la_sockets[idx].used) return;

    struct la_socket *s = &la_sockets[idx];
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
        peer->used = 0;
    }

    /* Free this slot */
    s->state = LA_SOCK_CLOSED;
    s->used  = 0;
    s->peer  = 0;
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
    if (src->type != LA_SOCK_DGRAM) return -1;

    /* Find destination UDP socket */
    int dst_idx = la_sock_find_udp(addr, port);
    if (dst_idx < 0) return -1;  /* no listener on this port */

    struct la_socket *dst = &la_sockets[dst_idx];

    if (len > LA_UDP_DGRAM_MAX)
        len = LA_UDP_DGRAM_MAX;

    /* Block if queue is full */
    while (dst->dgram_cnt >= LA_UDP_DGRAM_QUEUE) {
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
    dg->src_addr = src->laddr ? src->laddr : 0x0100007F;  /* 127.0.0.1 */
    dg->src_port = src->lport;
    dst->dgram_cnt++;

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
    if (s->type != LA_SOCK_DGRAM) return -1;

    /* Block while queue is empty */
    while (s->dgram_cnt == 0) {
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

    return (int)dgram_len;
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

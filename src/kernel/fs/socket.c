#include "mod.h"

#define AF_INET_LOCAL 2
#define SOCK_STREAM_LOCAL 1
#define SOCK_DGRAM_LOCAL 2
#define SOCK_NONBLOCK_LOCAL 0x800
#define SOCK_CLOEXEC_LOCAL 0x80000
#define IPPROTO_TCP_LOCAL 6
#define IPPROTO_UDP_LOCAL 17

#define SHUT_RD_LOCAL 0
#define SHUT_WR_LOCAL 1
#define SHUT_RDWR_LOCAL 2

#define SOL_SOCKET_LOCAL 1
#define SO_REUSEADDR_LOCAL 2
#define SO_TYPE_LOCAL 3
#define SO_ERROR_LOCAL 4
#define SO_SNDBUF_LOCAL 7
#define SO_RCVBUF_LOCAL 8
#define SO_KEEPALIVE_LOCAL 9
#define SO_RCVTIMEO_OLD_LOCAL 20
#define SO_SNDTIMEO_OLD_LOCAL 21
#define SO_RCVTIMEO_NEW_LOCAL 66
#define SO_SNDTIMEO_NEW_LOCAL 67

#define TCP_NODELAY_LOCAL 1
#define TCP_MAXSEG_LOCAL 2
#define TCP_CORK_LOCAL 3

#define MSG_DONTWAIT_LOCAL 0x40

#define POLLIN_LOCAL 0x001
#define POLLOUT_LOCAL 0x004
#define POLLERR_LOCAL 0x008
#define POLLHUP_LOCAL 0x010

#define INADDR_ANY_RAW 0
#define INADDR_LOOPBACK_RAW 0x0100007fU

#define N_SOCKET 32
#define SOCK_STREAM_BUF 32768
#define SOCK_UDP_QUEUE 128
#define SOCK_UDP_DATA 32768
#define SOCK_IO_CHUNK 1536
#define SOCK_ACCEPT_QUEUE 16
#define SOCK_HANDOFF_YIELDS 4
#define SOCK_ACCEPT_DRAIN_ATTEMPTS 8
#define SOCK_UDP_CLOSE_WAIT_TICKS 1

enum socket_state {
    SOCK_UNUSED = 0,
    SOCK_CREATED,
    SOCK_LISTEN,
    SOCK_CONNECTED,
    SOCK_CLOSED,
};

typedef struct udp_packet {
    uint32 len;
    uint16 src_port;
    uint32 src_addr;
    char data[SOCK_UDP_DATA];
} udp_packet_t;

struct socket {
    int used;
    int domain;
    int type;
    int protocol;
    int state;
    int nonblock;
    int reuseaddr;
    int bound;
    int read_closed;
    int write_closed;
    uint64 rcv_timeout_ticks;
    uint64 snd_timeout_ticks;
    uint16 local_port;
    uint32 local_addr;
    uint16 peer_port;
    uint32 peer_addr;
    struct socket *peer;

    char stream_buf[SOCK_STREAM_BUF];
    uint32 stream_r;
    uint32 stream_w;

    udp_packet_t udp_q[SOCK_UDP_QUEUE];
    uint32 udp_head;
    uint32 udp_tail;
    uint32 udp_count;

    struct socket *accept_q[SOCK_ACCEPT_QUEUE];
    uint32 accept_head;
    uint32 accept_tail;
    uint32 accept_count;
    int backlog;
};

static socket_t sockets[N_SOCKET];
static spinlock_t socket_lk;
static uint16 next_ephemeral = 49152;
static int socket_wait_anchor;

static uint16 bswap16(uint16 v)
{
    return (uint16)((v << 8) | (v >> 8));
}

static uint16 make_net_port(uint16 host_port)
{
    return bswap16(host_port);
}

static int socket_base_type(int type)
{
    return type & 0xf;
}

static int protocol_matches(int sock_type, int protocol)
{
    if (protocol == 0)
        return 1;
    if (sock_type == SOCK_STREAM_LOCAL)
        return protocol == IPPROTO_TCP_LOCAL;
    if (sock_type == SOCK_DGRAM_LOCAL)
        return protocol == IPPROTO_UDP_LOCAL;
    return 0;
}

static uint64 socket_timeval_to_ticks(uint64 optval, uint32 optlen)
{
    uint64 tv[2];

    if (optval == 0 || optlen < sizeof(tv))
        return 0;
    uvm_copyin(myproc()->pgtbl, (uint64)tv, optval, sizeof(tv));
    uint64 ticks = tv[0] * 10;
    if (tv[1] > 0)
        ticks++;
    return ticks;
}

static void socket_timeout_copyout(uint64 optval, uint64 optlen, uint64 ticks, int is_new)
{
    uint64 tv[2];
    uint32 len;

    uvm_copyin(myproc()->pgtbl, (uint64)&len, optlen, sizeof(len));
    tv[0] = ticks / 10;
    tv[1] = (ticks % 10) != 0 ? (is_new ? 100000000ULL : 100000ULL) : 0;
    if (len > sizeof(tv))
        len = sizeof(tv);
    uvm_copyout(myproc()->pgtbl, optval, (uint64)tv, len);
    len = sizeof(tv);
    uvm_copyout(myproc()->pgtbl, optlen, (uint64)&len, sizeof(len));
}

static int sockaddr_copyin(uint64 user_addr, uint32 addrlen, uint16 *port, uint32 *addr)
{
    uint8 raw[16];
    uint16 family;

    if (user_addr == 0 || addrlen < 16)
        return -EINVAL;
    uvm_copyin(myproc()->pgtbl, (uint64)raw, user_addr, sizeof(raw));
    family = *(uint16 *)&raw[0];
    if (family != AF_INET_LOCAL)
        return -EAFNOSUPPORT;
    *port = *(uint16 *)&raw[2];
    *addr = *(uint32 *)&raw[4];
    if (*addr != INADDR_ANY_RAW && *addr != INADDR_LOOPBACK_RAW)
        return -EADDRNOTAVAIL;
    return 0;
}

static void sockaddr_copyout(uint64 user_addr, uint64 user_addrlen, uint16 port, uint32 addr)
{
    uint32 len = 16;
    uint8 raw[16];

    if (user_addrlen != 0) {
        uvm_copyin(myproc()->pgtbl, (uint64)&len, user_addrlen, sizeof(len));
        if (len > 16)
            len = 16;
    }
    if (user_addr != 0 && len > 0) {
        memset(raw, 0, sizeof(raw));
        *(uint16 *)&raw[0] = AF_INET_LOCAL;
        *(uint16 *)&raw[2] = port;
        *(uint32 *)&raw[4] = addr == INADDR_ANY_RAW ? INADDR_LOOPBACK_RAW : addr;
        uvm_copyout(myproc()->pgtbl, user_addr, (uint64)raw, len);
    }
    if (user_addrlen != 0) {
        uint32 actual = 16;
        uvm_copyout(myproc()->pgtbl, user_addrlen, (uint64)&actual, sizeof(actual));
    }
}

static socket_t *socket_alloc_locked(void)
{
    for (int i = 0; i < N_SOCKET; i++) {
        if (!sockets[i].used) {
            memset(&sockets[i], 0, sizeof(sockets[i]));
            sockets[i].used = 1;
            sockets[i].state = SOCK_CREATED;
            sockets[i].local_addr = INADDR_LOOPBACK_RAW;
            return &sockets[i];
        }
    }
    return NULL;
}

static int port_in_use_locked(int type, uint16 port)
{
    for (int i = 0; i < N_SOCKET; i++) {
        socket_t *so = &sockets[i];
        if (!so->used || !so->bound)
            continue;
        if (so->type == type && so->local_port == port)
            return 1;
    }
    return 0;
}

static int bind_conflicts_locked(socket_t *candidate, uint16 port)
{
    for (int i = 0; i < N_SOCKET; i++) {
        socket_t *so = &sockets[i];
        if (so == candidate)
            continue;
        if (!so->used || !so->bound)
            continue;
        if (so->type != candidate->type || so->local_port != port)
            continue;
        if (candidate->type == SOCK_DGRAM_LOCAL && candidate->reuseaddr && so->reuseaddr)
            continue;
        return 1;
    }
    return 0;
}

static uint16 alloc_ephemeral_locked(int type)
{
    for (int tries = 0; tries < 20000; tries++) {
        uint16 host = next_ephemeral++;
        if (next_ephemeral >= 61000)
            next_ephemeral = 49152;
        uint16 port = make_net_port(host);
        if (!port_in_use_locked(type, port))
            return port;
    }
    return 0;
}

static int auto_bind_locked(socket_t *so)
{
    if (so->bound)
        return 0;
    so->local_port = alloc_ephemeral_locked(so->type);
    if (so->local_port == 0)
        return -EADDRINUSE;
    so->local_addr = INADDR_LOOPBACK_RAW;
    so->bound = 1;
    return 0;
}

static socket_t *find_udp_dst_locked(uint16 port, uint16 src_port, uint32 src_addr)
{
    socket_t *fallback = NULL;

    for (int i = 0; i < N_SOCKET; i++) {
        socket_t *so = &sockets[i];
        if (!so->used || !so->bound || so->type != SOCK_DGRAM_LOCAL || so->local_port != port)
            continue;
        if (so->state == SOCK_CONNECTED &&
            so->peer_port == src_port &&
            (so->peer_addr == src_addr || so->peer_addr == INADDR_ANY_RAW))
            return so;
        if (fallback == NULL && so->state != SOCK_CONNECTED)
            fallback = so;
    }

    return fallback;
}

static socket_t *find_listener_locked(uint16 port)
{
    for (int i = 0; i < N_SOCKET; i++) {
        socket_t *so = &sockets[i];
        if (!so->used || so->state != SOCK_LISTEN)
            continue;
        if (so->local_port == port)
            return so;
    }
    return NULL;
}

static int stream_accept_cleanup_pending_locked(uint16 port)
{
    for (int i = 0; i < N_SOCKET; i++) {
        socket_t *so = &sockets[i];
        if (!so->used || so->type != SOCK_STREAM_LOCAL)
            continue;
        if (so->state != SOCK_CONNECTED || !so->bound || so->local_port != port)
            continue;
        if (so->peer == NULL || so->read_closed)
            return 1;
    }
    return 0;
}

static uint32 stream_avail_locked(socket_t *so)
{
    return so->stream_w - so->stream_r;
}

static uint32 stream_space_locked(socket_t *so)
{
    return SOCK_STREAM_BUF - stream_avail_locked(so);
}

static void socket_handoff_peer(void)
{
    for (int i = 0; i < SOCK_HANDOFF_YIELDS; i++) {
        myproc()->mlfq_yield_reason = MLFQ_YIELD_VOLUNTARY;
        proc_yield();
    }
}

static void socket_drop_locked(socket_t *so)
{
    if (so == NULL || !so->used)
        return;

    if (so->state == SOCK_LISTEN) {
        while (so->accept_count > 0) {
            socket_t *child = so->accept_q[so->accept_head];
            so->accept_head = (so->accept_head + 1) % SOCK_ACCEPT_QUEUE;
            so->accept_count--;
            if (child != NULL && child->used) {
                if (child->peer != NULL) {
                    child->peer->peer = NULL;
                    child->peer->read_closed = 1;
                }
                child->used = 0;
            }
        }
    }

    so->read_closed = 1;
    so->write_closed = 1;
    so->state = SOCK_CLOSED;
    if (so->peer != NULL) {
        socket_t *peer = so->peer;
        peer->peer = NULL;
        peer->read_closed = 1;
        so->peer = NULL;
    }
    so->used = 0;
}

void socket_init(void)
{
    spinlock_init(&socket_lk, "socket");
    spinlock_acquire(&socket_lk);
    memset(sockets, 0, sizeof(sockets));
    next_ephemeral = 49152;
    spinlock_release(&socket_lk);
}

void *socket_wait_channel(void)
{
    return &socket_wait_anchor;
}

void socket_wait(void)
{
    spinlock_acquire(&socket_lk);
    proc_sleep(&socket_wait_anchor, &socket_lk);
    spinlock_release(&socket_lk);
}

file_t *socket_file_alloc(int domain, int type, int protocol, uint8 *cloexec)
{
    int base_type = socket_base_type(type);
    socket_t *so;
    file_t *file;

    if (domain != AF_INET_LOCAL)
        return NULL;
    if (base_type != SOCK_STREAM_LOCAL && base_type != SOCK_DGRAM_LOCAL)
        return NULL;
    if (!protocol_matches(base_type, protocol))
        return NULL;

    file = file_alloc();
    if (file == NULL)
        return NULL;

    spinlock_acquire(&socket_lk);
    so = socket_alloc_locked();
    if (so == NULL) {
        spinlock_release(&socket_lk);
        file_close(file);
        return NULL;
    }
    so->domain = domain;
    so->type = base_type;
    so->protocol = protocol == 0 ? (base_type == SOCK_STREAM_LOCAL ? IPPROTO_TCP_LOCAL : IPPROTO_UDP_LOCAL) : protocol;
    so->nonblock = (type & SOCK_NONBLOCK_LOCAL) ? 1 : 0;
    spinlock_release(&socket_lk);

    file->is_socket = true;
    file->socket = so;
    file->readable = true;
    file->writbale = true;
    if (cloexec != NULL)
        *cloexec = (type & SOCK_CLOEXEC_LOCAL) ? 1 : 0;
    return file;
}

void socket_file_close(socket_t *so)
{
    int handoff_peer = 0;
    int wait_udp_close = 0;

    spinlock_acquire(&socket_lk);
    if (so != NULL && so->used && so->type == SOCK_STREAM_LOCAL &&
        so->peer != NULL && so->peer->used)
        handoff_peer = 1;
    if (so != NULL && so->used && so->type == SOCK_DGRAM_LOCAL) {
        handoff_peer = 1;
        wait_udp_close = 1;
    }
    socket_drop_locked(so);
    proc_wakeup(&socket_wait_anchor);
    spinlock_release(&socket_lk);

    if (handoff_peer)
        socket_handoff_peer();
    if (wait_udp_close)
        timer_wait(SOCK_UDP_CLOSE_WAIT_TICKS);
}

uint32 socket_file_read(socket_t *so, uint64 dst, uint32 len, bool is_user_dst)
{
    int ret = socket_recvfrom(so, dst, len, 0, 0, 0);
    return ret < 0 ? (uint32)-1 : (uint32)ret;
}

uint32 socket_file_write(socket_t *so, uint64 src, uint32 len, bool is_user_src)
{
    if (!is_user_src)
        return (uint32)-1;
    int ret = socket_sendto(so, src, len, 0, 0, 0);
    return ret < 0 ? (uint32)-1 : (uint32)ret;
}

int socket_bind(socket_t *so, uint64 user_addr, uint32 addrlen)
{
    uint16 port;
    uint32 addr;
    int ret = sockaddr_copyin(user_addr, addrlen, &port, &addr);
    if (ret < 0)
        return ret;

    spinlock_acquire(&socket_lk);
    if (so == NULL || !so->used) {
        spinlock_release(&socket_lk);
        return -ENOTSOCK;
    }
    if (so->bound) {
        spinlock_release(&socket_lk);
        return -EINVAL;
    }
    if (port == 0)
        port = alloc_ephemeral_locked(so->type);
    if (port == 0 || bind_conflicts_locked(so, port)) {
        spinlock_release(&socket_lk);
        return -EADDRINUSE;
    }
    so->bound = 1;
    so->local_port = port;
    so->local_addr = addr == INADDR_ANY_RAW ? INADDR_LOOPBACK_RAW : addr;
    proc_wakeup(&socket_wait_anchor);
    spinlock_release(&socket_lk);
    return 0;
}

int socket_listen(socket_t *so, int backlog)
{
    spinlock_acquire(&socket_lk);
    if (so == NULL || !so->used) {
        spinlock_release(&socket_lk);
        return -ENOTSOCK;
    }
    if (so->type != SOCK_STREAM_LOCAL) {
        spinlock_release(&socket_lk);
        return -EOPNOTSUPP;
    }
    if (auto_bind_locked(so) < 0) {
        spinlock_release(&socket_lk);
        return -EADDRINUSE;
    }
    so->state = SOCK_LISTEN;
    so->backlog = backlog <= 0 ? 1 : backlog;
    if (so->backlog > SOCK_ACCEPT_QUEUE)
        so->backlog = SOCK_ACCEPT_QUEUE;
    proc_wakeup(&socket_wait_anchor);
    spinlock_release(&socket_lk);
    return 0;
}

int socket_accept(socket_t *listener, uint64 user_addr, uint64 user_addrlen, int flags, file_t **out_file, uint8 *out_cloexec)
{
    socket_t *child;
    file_t *file;

    spinlock_acquire(&socket_lk);
    if (listener == NULL || !listener->used) {
        spinlock_release(&socket_lk);
        return -ENOTSOCK;
    }
    if (listener->type != SOCK_STREAM_LOCAL) {
        spinlock_release(&socket_lk);
        return -EOPNOTSUPP;
    }
    if (listener->state != SOCK_LISTEN) {
        spinlock_release(&socket_lk);
        return -EINVAL;
    }
    while (listener->accept_count == 0) {
        if (myproc()->sig_pending != 0) {
            spinlock_release(&socket_lk);
            return -EINTR;
        }
        if (listener->nonblock || (flags & SOCK_NONBLOCK_LOCAL)) {
            spinlock_release(&socket_lk);
            return -EAGAIN;
        }
        proc_sleep(&socket_wait_anchor, &socket_lk);
        if (!listener->used || listener->state != SOCK_LISTEN) {
            spinlock_release(&socket_lk);
            return -EINVAL;
        }
    }
    child = listener->accept_q[listener->accept_head];
    listener->accept_head = (listener->accept_head + 1) % SOCK_ACCEPT_QUEUE;
    listener->accept_count--;
    spinlock_release(&socket_lk);

    file = file_alloc();
    if (file == NULL) {
        socket_file_close(child);
        return -EMFILE;
    }
    file->is_socket = true;
    file->socket = child;
    file->readable = true;
    file->writbale = true;

    spinlock_acquire(&socket_lk);
    if (child != NULL && child->used)
        child->nonblock = (flags & SOCK_NONBLOCK_LOCAL) ? 1 : 0;
    spinlock_release(&socket_lk);

    if (child != NULL)
        sockaddr_copyout(user_addr, user_addrlen, child->peer_port, child->peer_addr);
    if (out_file != NULL)
        *out_file = file;
    if (out_cloexec != NULL)
        *out_cloexec = (flags & SOCK_CLOEXEC_LOCAL) ? 1 : 0;
    return 0;
}

int socket_connect(socket_t *so, uint64 user_addr, uint32 addrlen)
{
    uint16 port;
    uint32 addr;
    int yield_to_listener = 0;
    int ret = sockaddr_copyin(user_addr, addrlen, &port, &addr);
    if (ret < 0)
        return ret;

    spinlock_acquire(&socket_lk);
    if (so == NULL || !so->used) {
        spinlock_release(&socket_lk);
        return -ENOTSOCK;
    }
    if (so->state == SOCK_CONNECTED) {
        spinlock_release(&socket_lk);
        return -EISCONN;
    }
    ret = auto_bind_locked(so);
    if (ret < 0) {
        spinlock_release(&socket_lk);
        return ret;
    }
    so->peer_port = port;
    so->peer_addr = addr == INADDR_ANY_RAW ? INADDR_LOOPBACK_RAW : addr;

    if (so->type == SOCK_DGRAM_LOCAL) {
        so->state = SOCK_CONNECTED;
        spinlock_release(&socket_lk);
        return 0;
    }

    socket_t *listener = NULL;
    for (int attempt = 0; attempt < 50; attempt++) {
        listener = find_listener_locked(port);
        if (listener != NULL)
            break;
        if (so->nonblock)
            break;
        spinlock_release(&socket_lk);
        timer_wait(1);
        spinlock_acquire(&socket_lk);
        if (!so->used) {
            spinlock_release(&socket_lk);
            return -ENOTSOCK;
        }
    }
    if (listener == NULL || listener->accept_count >= (uint32)listener->backlog) {
        spinlock_release(&socket_lk);
        return -ECONNREFUSED;
    }
    for (int attempt = 0; attempt < SOCK_ACCEPT_DRAIN_ATTEMPTS &&
                          stream_accept_cleanup_pending_locked(listener->local_port); attempt++) {
        proc_wakeup(&socket_wait_anchor);
        spinlock_release(&socket_lk);
        socket_handoff_peer();
        timer_wait(1);
        spinlock_acquire(&socket_lk);
        if (!so->used) {
            spinlock_release(&socket_lk);
            return -ENOTSOCK;
        }
        listener = find_listener_locked(port);
        if (listener == NULL || listener->accept_count >= (uint32)listener->backlog) {
            spinlock_release(&socket_lk);
            return -ECONNREFUSED;
        }
    }

    socket_t *server = socket_alloc_locked();
    if (server == NULL) {
        spinlock_release(&socket_lk);
        return -ENOBUFS;
    }
    server->domain = AF_INET_LOCAL;
    server->type = SOCK_STREAM_LOCAL;
    server->protocol = IPPROTO_TCP_LOCAL;
    server->state = SOCK_CONNECTED;
    server->bound = 1;
    server->local_port = listener->local_port;
    server->local_addr = listener->local_addr;
    server->peer_port = so->local_port;
    server->peer_addr = so->local_addr;
    server->peer = so;

    so->state = SOCK_CONNECTED;
    so->peer = server;

    listener->accept_q[listener->accept_tail] = server;
    listener->accept_tail = (listener->accept_tail + 1) % SOCK_ACCEPT_QUEUE;
    listener->accept_count++;
    yield_to_listener = 1;
    proc_wakeup(&socket_wait_anchor);
    spinlock_release(&socket_lk);
    if (yield_to_listener)
        socket_handoff_peer();
    return 0;
}

int socket_getname(socket_t *so, uint64 user_addr, uint64 user_addrlen, bool peer)
{
    uint16 port;
    uint32 addr;

    spinlock_acquire(&socket_lk);
    if (so == NULL || !so->used) {
        spinlock_release(&socket_lk);
        return -ENOTSOCK;
    }
    if (peer && so->state != SOCK_CONNECTED) {
        spinlock_release(&socket_lk);
        return -ENOTCONN;
    }
    port = peer ? so->peer_port : so->local_port;
    addr = peer ? so->peer_addr : so->local_addr;
    spinlock_release(&socket_lk);

    sockaddr_copyout(user_addr, user_addrlen, port, addr);
    return 0;
}

int socket_sendto(socket_t *so, uint64 user_buf, uint32 len, int flags, uint64 user_addr, uint32 addrlen)
{
    char tmp[SOCK_IO_CHUNK];
    uint32 copied = 0;
    int dontwait = (flags & MSG_DONTWAIT_LOCAL) != 0;

    if (so == NULL)
        return -ENOTSOCK;

    spinlock_acquire(&socket_lk);
    if (!so->used) {
        spinlock_release(&socket_lk);
        return -ENOTSOCK;
    }
    int type = so->type;
    spinlock_release(&socket_lk);

    if (type == SOCK_DGRAM_LOCAL) {
        uint16 port = 0;
        uint32 addr = INADDR_LOOPBACK_RAW;
        if (user_addr != 0) {
            int ret = sockaddr_copyin(user_addr, addrlen, &port, &addr);
            if (ret < 0)
                return ret;
        }
        if (len > SOCK_UDP_DATA)
            return -EMSGSIZE;
        spinlock_acquire(&socket_lk);
        if (user_addr == 0) {
            if (so->state != SOCK_CONNECTED) {
                spinlock_release(&socket_lk);
                return -EDESTADDRREQ;
            }
            port = so->peer_port;
            addr = so->peer_addr;
        }
        int ret = auto_bind_locked(so);
        if (ret < 0) {
            spinlock_release(&socket_lk);
            return ret;
        }
        socket_t *dst = find_udp_dst_locked(port, so->local_port, so->local_addr);
        if (dst == NULL) {
            spinlock_release(&socket_lk);
            return -ECONNREFUSED;
        }
        if (dst->udp_count >= SOCK_UDP_QUEUE) {
            proc_wakeup(&socket_wait_anchor);
            spinlock_release(&socket_lk);
            return (int)len;
        }
        udp_packet_t *pkt = &dst->udp_q[dst->udp_tail];
        pkt->len = len;
        pkt->src_port = so->local_port;
        pkt->src_addr = so->local_addr;
        if (len > 0)
            uvm_copyin(myproc()->pgtbl, (uint64)pkt->data, user_buf, len);
        dst->udp_tail = (dst->udp_tail + 1) % SOCK_UDP_QUEUE;
        dst->udp_count++;
        (void)addr;
        proc_wakeup(&socket_wait_anchor);
        spinlock_release(&socket_lk);
        return (int)len;
    }

    while (copied < len) {
        uint32 chunk = len - copied;
        if (chunk > sizeof(tmp))
            chunk = sizeof(tmp);
        uvm_copyin(myproc()->pgtbl, (uint64)tmp, user_buf + copied, chunk);

        uint32 done = 0;
        uint64 timeout_ticks = (uint64)-1;
        while (done < chunk) {
            spinlock_acquire(&socket_lk);
            if (!so->used || so->write_closed) {
                spinlock_release(&socket_lk);
                return copied > 0 ? (int)copied : -EPIPE;
            }
            if (so->state != SOCK_CONNECTED || so->peer == NULL || !so->peer->used) {
                spinlock_release(&socket_lk);
                return copied > 0 ? (int)copied : -ENOTCONN;
            }
            socket_t *peer = so->peer;
            if (peer->read_closed) {
                spinlock_release(&socket_lk);
                return copied > 0 ? (int)copied : -EPIPE;
            }
            uint32 space = stream_space_locked(peer);
            if (space == 0) {
                if (so->nonblock || dontwait) {
                    spinlock_release(&socket_lk);
                    return copied > 0 ? (int)copied : -EAGAIN;
                }
                if (timeout_ticks == (uint64)-1)
                    timeout_ticks = so->snd_timeout_ticks;
                if (timeout_ticks > 0) {
                    spinlock_release(&socket_lk);
                    timer_wait(1);
                    spinlock_acquire(&socket_lk);
                    if (!so->used || so->write_closed) {
                        spinlock_release(&socket_lk);
                        return copied > 0 ? (int)copied : -EPIPE;
                    }
                    timeout_ticks--;
                    if (timeout_ticks == 0) {
                        spinlock_release(&socket_lk);
                        return copied > 0 ? (int)copied : -EAGAIN;
                    }
                    spinlock_release(&socket_lk);
                    continue;
                }
                proc_sleep(&socket_wait_anchor, &socket_lk);
                spinlock_release(&socket_lk);
                continue;
            }
            uint32 n = chunk - done;
            if (n > space)
                n = space;
            for (uint32 i = 0; i < n; i++) {
                peer->stream_buf[peer->stream_w % SOCK_STREAM_BUF] = tmp[done + i];
                peer->stream_w++;
            }
            proc_wakeup(&socket_wait_anchor);
            spinlock_release(&socket_lk);
            done += n;
            copied += n;
        }
    }
    return (int)copied;
}

int socket_recvfrom(socket_t *so, uint64 user_buf, uint32 len, int flags, uint64 user_addr, uint64 user_addrlen)
{
    char tmp[SOCK_IO_CHUNK];
    int dontwait = (flags & MSG_DONTWAIT_LOCAL) != 0;

    if (so == NULL)
        return -ENOTSOCK;

    spinlock_acquire(&socket_lk);
    if (!so->used) {
        spinlock_release(&socket_lk);
        return -ENOTSOCK;
    }
    uint64 timeout_ticks = so->rcv_timeout_ticks;

    if (so->type == SOCK_DGRAM_LOCAL) {
        while (so->udp_count == 0) {
            if (so->nonblock || dontwait) {
                spinlock_release(&socket_lk);
                return -EAGAIN;
            }
            if (timeout_ticks > 0) {
                spinlock_release(&socket_lk);
                timer_wait(1);
                spinlock_acquire(&socket_lk);
                if (!so->used) {
                    spinlock_release(&socket_lk);
                    return -ENOTSOCK;
                }
                if (so->udp_count == 0) {
                    timeout_ticks--;
                    if (timeout_ticks == 0) {
                        spinlock_release(&socket_lk);
                        return -EAGAIN;
                    }
                }
                continue;
            }
            proc_sleep(&socket_wait_anchor, &socket_lk);
            if (!so->used) {
                spinlock_release(&socket_lk);
                return -ENOTSOCK;
            }
        }
        udp_packet_t *pkt = &so->udp_q[so->udp_head];
        uint32 n = pkt->len;
        if (n > len)
            n = len;
        uint16 src_port = pkt->src_port;
        uint32 src_addr = pkt->src_addr;
        if (n > 0)
            uvm_copyout(myproc()->pgtbl, user_buf, (uint64)pkt->data, n);
        so->udp_head = (so->udp_head + 1) % SOCK_UDP_QUEUE;
        so->udp_count--;
        proc_wakeup(&socket_wait_anchor);
        spinlock_release(&socket_lk);

        sockaddr_copyout(user_addr, user_addrlen, src_port, src_addr);
        return (int)n;
    }

    while (stream_avail_locked(so) == 0 && !so->read_closed && so->peer != NULL) {
        if (so->nonblock || dontwait) {
            spinlock_release(&socket_lk);
            return -EAGAIN;
        }
        if (timeout_ticks > 0) {
            spinlock_release(&socket_lk);
            timer_wait(1);
            spinlock_acquire(&socket_lk);
            if (!so->used) {
                spinlock_release(&socket_lk);
                return -ENOTSOCK;
            }
            if (stream_avail_locked(so) == 0 && !so->read_closed && so->peer != NULL) {
                timeout_ticks--;
                if (timeout_ticks == 0) {
                    spinlock_release(&socket_lk);
                    return -EAGAIN;
                }
            }
            continue;
        }
        proc_sleep(&socket_wait_anchor, &socket_lk);
        if (!so->used) {
            spinlock_release(&socket_lk);
            return -ENOTSOCK;
        }
    }
    uint32 avail = stream_avail_locked(so);
    if (avail == 0) {
        spinlock_release(&socket_lk);
        return 0;
    }
    uint32 n = len;
    if (n > avail)
        n = avail;
    if (n > sizeof(tmp))
        n = sizeof(tmp);
    for (uint32 i = 0; i < n; i++) {
        tmp[i] = so->stream_buf[so->stream_r % SOCK_STREAM_BUF];
        so->stream_r++;
    }
    uint16 src_port = so->peer_port;
    uint32 src_addr = so->peer_addr;
    proc_wakeup(&socket_wait_anchor);
    spinlock_release(&socket_lk);

    if (n > 0)
        uvm_copyout(myproc()->pgtbl, user_buf, (uint64)tmp, n);
    sockaddr_copyout(user_addr, user_addrlen, src_port, src_addr);
    return (int)n;
}

int socket_setsockopt(socket_t *so, int level, int optname, uint64 optval, uint32 optlen)
{
    int value = 0;

    if (so == NULL)
        return -ENOTSOCK;
    if (level == SOL_SOCKET_LOCAL && optname == SO_REUSEADDR_LOCAL) {
        if (optval != 0 && optlen >= sizeof(value))
            uvm_copyin(myproc()->pgtbl, (uint64)&value, optval, sizeof(value));
        spinlock_acquire(&socket_lk);
        if (!so->used) {
            spinlock_release(&socket_lk);
            return -ENOTSOCK;
        }
        so->reuseaddr = value != 0;
        spinlock_release(&socket_lk);
        return 0;
    }
    if (level == SOL_SOCKET_LOCAL &&
        (optname == SO_RCVTIMEO_OLD_LOCAL || optname == SO_RCVTIMEO_NEW_LOCAL ||
         optname == SO_SNDTIMEO_OLD_LOCAL || optname == SO_SNDTIMEO_NEW_LOCAL)) {
        uint64 ticks = socket_timeval_to_ticks(optval, optlen);
        spinlock_acquire(&socket_lk);
        if (!so->used) {
            spinlock_release(&socket_lk);
            return -ENOTSOCK;
        }
        if (optname == SO_RCVTIMEO_OLD_LOCAL || optname == SO_RCVTIMEO_NEW_LOCAL)
            so->rcv_timeout_ticks = ticks;
        else
            so->snd_timeout_ticks = ticks;
        spinlock_release(&socket_lk);
        return 0;
    }
    return 0;
}

int socket_getsockopt(socket_t *so, int level, int optname, uint64 optval, uint64 optlen)
{
    int value = 0;
    uint32 len;

    if (so == NULL)
        return -ENOTSOCK;
    if (optval == 0 || optlen == 0)
        return -EFAULT;

    uvm_copyin(myproc()->pgtbl, (uint64)&len, optlen, sizeof(len));
    if (level == SOL_SOCKET_LOCAL &&
        (optname == SO_RCVTIMEO_OLD_LOCAL || optname == SO_RCVTIMEO_NEW_LOCAL ||
         optname == SO_SNDTIMEO_OLD_LOCAL || optname == SO_SNDTIMEO_NEW_LOCAL)) {
        uint64 ticks;

        spinlock_acquire(&socket_lk);
        if (!so->used) {
            spinlock_release(&socket_lk);
            return -ENOTSOCK;
        }
        if (optname == SO_RCVTIMEO_OLD_LOCAL || optname == SO_RCVTIMEO_NEW_LOCAL)
            ticks = so->rcv_timeout_ticks;
        else
            ticks = so->snd_timeout_ticks;
        spinlock_release(&socket_lk);
        socket_timeout_copyout(optval, optlen, ticks,
                               optname == SO_RCVTIMEO_NEW_LOCAL || optname == SO_SNDTIMEO_NEW_LOCAL);
        return 0;
    }
    if (level == SOL_SOCKET_LOCAL && (optname == SO_SNDBUF_LOCAL || optname == SO_RCVBUF_LOCAL))
        value = 32000;
    else if (level == SOL_SOCKET_LOCAL && optname == SO_TYPE_LOCAL)
        value = so->type;
    else if (level == SOL_SOCKET_LOCAL && optname == SO_ERROR_LOCAL)
        value = 0;
    else if (level == IPPROTO_TCP_LOCAL && optname == TCP_MAXSEG_LOCAL)
        value = 1460;
    else if (level == IPPROTO_TCP_LOCAL && (optname == TCP_NODELAY_LOCAL || optname == TCP_CORK_LOCAL))
        value = 0;
    else if (level == SOL_SOCKET_LOCAL && optname == SO_REUSEADDR_LOCAL)
        value = so->reuseaddr;
    else if (level == SOL_SOCKET_LOCAL && optname == SO_KEEPALIVE_LOCAL)
        value = 0;
    else
        value = 0;

    if (len > sizeof(value))
        len = sizeof(value);
    uvm_copyout(myproc()->pgtbl, optval, (uint64)&value, len);
    len = sizeof(value);
    uvm_copyout(myproc()->pgtbl, optlen, (uint64)&len, sizeof(len));
    return 0;
}

int socket_shutdown(socket_t *so, int how)
{
    if (how < SHUT_RD_LOCAL || how > SHUT_RDWR_LOCAL)
        return -EINVAL;
    spinlock_acquire(&socket_lk);
    if (so == NULL || !so->used) {
        spinlock_release(&socket_lk);
        return -ENOTSOCK;
    }
    if (how == SHUT_RD_LOCAL || how == SHUT_RDWR_LOCAL)
        so->read_closed = 1;
    if (how == SHUT_WR_LOCAL || how == SHUT_RDWR_LOCAL) {
        so->write_closed = 1;
        if (so->peer != NULL)
            so->peer->read_closed = 1;
    }
    proc_wakeup(&socket_wait_anchor);
    spinlock_release(&socket_lk);
    return 0;
}

int socket_get_nonblock(socket_t *so)
{
    int nonblock;

    spinlock_acquire(&socket_lk);
    if (so == NULL || !so->used) {
        spinlock_release(&socket_lk);
        return -ENOTSOCK;
    }
    nonblock = so->nonblock;
    spinlock_release(&socket_lk);
    return nonblock;
}

int socket_set_nonblock(socket_t *so, int nonblock)
{
    spinlock_acquire(&socket_lk);
    if (so == NULL || !so->used) {
        spinlock_release(&socket_lk);
        return -ENOTSOCK;
    }
    so->nonblock = nonblock ? 1 : 0;
    proc_wakeup(&socket_wait_anchor);
    spinlock_release(&socket_lk);
    return 0;
}

int socket_poll_ready(socket_t *so, int events)
{
    int revents = 0;

    spinlock_acquire(&socket_lk);
    if (so == NULL || !so->used) {
        spinlock_release(&socket_lk);
        return POLLERR_LOCAL;
    }
    if (events & POLLIN_LOCAL) {
        if (so->state == SOCK_LISTEN) {
            if (so->accept_count > 0)
                revents |= POLLIN_LOCAL;
        } else if (so->type == SOCK_DGRAM_LOCAL && so->udp_count > 0) {
            revents |= POLLIN_LOCAL;
        } else if (so->type == SOCK_STREAM_LOCAL &&
                   (stream_avail_locked(so) > 0 || so->read_closed || so->peer == NULL)) {
            revents |= POLLIN_LOCAL;
        }
    }
    if (events & POLLOUT_LOCAL) {
        if (so->type == SOCK_DGRAM_LOCAL)
            revents |= POLLOUT_LOCAL;
        else if (so->state == SOCK_CONNECTED && so->peer != NULL &&
                 !so->write_closed && !so->peer->read_closed &&
                 stream_space_locked(so->peer) > 0)
            revents |= POLLOUT_LOCAL;
    }
    if (so->read_closed || (so->state == SOCK_CONNECTED && so->peer == NULL))
        revents |= POLLHUP_LOCAL;
    spinlock_release(&socket_lk);
    return revents;
}

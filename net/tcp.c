/* ch9: 网络协议栈 - TCP层实现 */
#include "platform.h"
#include "util.h"
#include "ip.h"
#include "tcp.h"

#define TCP_FLG_FIN 0x01
#define TCP_FLG_SYN 0x02
#define TCP_FLG_RST 0x04
#define TCP_FLG_PSH 0x08
#define TCP_FLG_ACK 0x10
#define TCP_FLG_URG 0x20

#define TCP_FLG_IS(x, y) ((x & 0x3f) == (y))
#define TCP_FLG_ISSET(x, y) ((x & 0x3f) & (y) ? 1 : 0)

#define TCP_PCB_SIZE 16

#define TCP_PCB_MODE_RFC793 0
#define TCP_PCB_MODE_SOCKET 1

#define TCP_PCB_STATE_FREE         0
#define TCP_PCB_STATE_CLOSED       1
#define TCP_PCB_STATE_LISTEN       2
#define TCP_PCB_STATE_SYN_SENT     3
#define TCP_PCB_STATE_SYN_RECEIVED 4
#define TCP_PCB_STATE_ESTABLISHED  5
#define TCP_PCB_STATE_FIN_WAIT1    6
#define TCP_PCB_STATE_FIN_WAIT2    7
#define TCP_PCB_STATE_CLOSING      8
#define TCP_PCB_STATE_TIME_WAIT    9
#define TCP_PCB_STATE_CLOSE_WAIT  10
#define TCP_PCB_STATE_LAST_ACK    11

#define TCP_DEFAULT_RTO 200000 /* micro seconds */
#define TCP_RETRANSMIT_DEADLINE 12 /* seconds */

#define TCP_SOURCE_PORT_MIN 49152
#define TCP_SOURCE_PORT_MAX 65535

struct pseudo_hdr {
    uint32 src;
    uint32 dst;
    uint8 zero;
    uint8 protocol;
    uint16 len;
};

struct tcp_hdr {
    uint16 src;
    uint16 dst;
    uint32 seq;
    uint32 ack;
    uint8 off;
    uint8 flg;
    uint16 wnd;
    uint16 sum;
    uint16 up;
};

struct tcp_segment_info {
    uint32 seq;
    uint32 ack;
    uint16 len;
    uint16 wnd;
    uint16 up;
};

struct tcp_pcb {
    int state;
    int mode; /* user command mode */
    struct ip_endpoint local;
    struct ip_endpoint foreign;
    struct {
        uint32 nxt;
        uint32 una;
        uint16 wnd;
        uint16 up;
        uint32 wl1;
        uint32 wl2;
    } snd;
    uint32 iss;
    struct {
        uint32 nxt;
        uint16 wnd;
        uint16 up;
    } rcv;
    uint32 irs;
    uint16 mtu;
    uint16 mss;
    uint8 buf[65535]; /* receive buffer */
    struct sched_ctx ctx;
    struct queue_head queue; /* retransmit queue */
    struct tcp_pcb *parent;
    struct queue_head backlog;
};

struct tcp_queue_entry {
    struct timeval first;
    struct timeval last;
    unsigned int rto; /* micro seconds */
    uint32 seq;
    uint8 flg;
    uint len;
    uint8 data[];
};

static mutex_t mutex = MUTEX_INITIALIZER;
static struct tcp_pcb pcbs[TCP_PCB_SIZE];

/*
 * TCP Protocol Control Block (PCB)
 *
 * NOTE: TCP PCB functions must be called after mutex locked
 */

static struct tcp_pcb *
tcp_pcb_alloc(void)
{
    struct tcp_pcb *pcb;

    for (pcb = pcbs; pcb < tailof(pcbs); pcb++) {
        if (pcb->state == TCP_PCB_STATE_FREE) {
            pcb->state = TCP_PCB_STATE_CLOSED;
            sched_ctx_init(&pcb->ctx);
            return pcb;
        }
    }
    return 0;
}

static void
tcp_pcb_release(struct tcp_pcb *pcb)
{
    struct queue_entry *entry;
    struct tcp_pcb *est;
    char ep1[IP_ENDPOINT_STR_LEN];
    char ep2[IP_ENDPOINT_STR_LEN];

    if (sched_ctx_destroy(&pcb->ctx) == -1) {
        sched_wakeup(&pcb->ctx);
        return;
    }
    while ((entry = queue_pop(&pcb->queue)) != 0) {
        memory_free(entry);
    }
    while ((est = queue_pop(&pcb->backlog)) != 0) {
        tcp_pcb_release(est);
    }
    debugf("released, local=%s, foreign=%s",
        ip_endpoint_ntop(&pcb->local, ep1, sizeof(ep1)),
        ip_endpoint_ntop(&pcb->foreign, ep2, sizeof(ep2)));
    memset(pcb, 0, sizeof(*pcb)); /* pcb->state is set to TCP_PCB_STATE_FREE (0) */
}

static struct tcp_pcb *
tcp_pcb_select(struct ip_endpoint *local, struct ip_endpoint *foreign)
{
    struct tcp_pcb *pcb, *listen_pcb = 0;

    for (pcb = pcbs; pcb < tailof(pcbs); pcb++) {
        if ((pcb->local.addr == IP_ADDR_ANY || pcb->local.addr == local->addr) && pcb->local.port == local->port) {
            if (!foreign) {
                return pcb;
            }
            if (pcb->foreign.addr == foreign->addr && pcb->foreign.port == foreign->port) {
                return pcb;
            }
            if (pcb->state == TCP_PCB_STATE_LISTEN) {
                if (pcb->foreign.addr == IP_ADDR_ANY && pcb->foreign.port == 0) {
                    /* LISTENed with wildcard foreign address/port */
                    listen_pcb = pcb;
                }
            }
        }
    }
    return listen_pcb;
}

static struct tcp_pcb *
tcp_pcb_get(int id)
{
    struct tcp_pcb *pcb;

    if (id < 0 || id >= (int)countof(pcbs)) {
        /* out of range */
        return 0;
    }
    pcb = &pcbs[id];
    if (pcb->state == TCP_PCB_STATE_FREE) {
        return 0;
    }
    return pcb;
}

static int
tcp_pcb_id(struct tcp_pcb *pcb)
{
    return indexof(pcbs, pcb);
}

static int
tcp_output_segment(uint32 seq, uint32 ack, uint8 flg, uint16 wnd, uint8 *data, uint len, struct ip_endpoint *local, struct ip_endpoint *foreign)
{
    uint8 *buf;
    struct tcp_hdr *hdr;
    struct pseudo_hdr pseudo;
    uint16 psum;
    uint16 total;
    char ep1[IP_ENDPOINT_STR_LEN];
    char ep2[IP_ENDPOINT_STR_LEN];

    buf = memory_alloc(IP_PAYLOAD_SIZE_MAX);
    if (!buf) {
        errorf("memory_alloc() failure");
        return -1;
    }
    hdr = (struct tcp_hdr *)buf;
    hdr->src = local->port;
    hdr->dst = foreign->port;
    hdr->seq = hton32(seq);
    hdr->ack = hton32(ack);
    hdr->off = (sizeof(*hdr) >> 2) << 4;
    hdr->flg = flg;
    hdr->wnd = hton16(wnd);
    hdr->sum = 0;
    hdr->up = 0;
    memmove(hdr + 1, data, len);
    pseudo.src = local->addr;
    pseudo.dst = foreign->addr;
    pseudo.zero = 0;
    pseudo.protocol = IP_PROTOCOL_TCP;
    total = sizeof(*hdr) + len;
    pseudo.len = hton16(total);
    psum = ~cksum16((uint16 *)&pseudo, sizeof(pseudo), 0);
    hdr->sum = cksum16((uint16 *)hdr, total, psum);
    debugf("%s => %s, len=%u (payload=%u)",
        ip_endpoint_ntop(local, ep1, sizeof(ep1)),
        ip_endpoint_ntop(foreign, ep2, sizeof(ep2)),
        total, len);
    if (ip_output(IP_PROTOCOL_TCP, (uint8 *)hdr, total, local->addr, foreign->addr) == -1) {
        memory_free(buf);
        return -1;
    }
    memory_free(buf);
    return len;
}

/*
 * TCP Retransmit
 *
 * NOTE: TCP Retransmit functions must be called after mutex locked
 */

static int
tcp_retransmit_queue_add(struct tcp_pcb *pcb, uint32 seq, uint8 flg, uint8 *data, uint len)
{
    struct tcp_queue_entry *entry;

    entry = memory_alloc(sizeof(*entry) + len);
    if (!entry) {
        errorf("memory_alloc() failure");
        return -1;
    }
    entry->rto = TCP_DEFAULT_RTO;
    entry->seq = seq;
    entry->flg = flg;
    entry->len = len;
    memmove(entry->data, data, entry->len);
    gettimeofday(&entry->first, 0);
    entry->last = entry->first;
    if (!queue_push(&pcb->queue, entry)) {
        errorf("queue_push() failure");
        memory_free(entry);
        return -1;
    }
    return 0;
}

static void
tcp_retransmit_queue_cleanup(struct tcp_pcb *pcb)
{
    struct tcp_queue_entry *entry;

    while (1) {
        entry = queue_peek(&pcb->queue);
        if (!entry) {
            break;
        }
        if (entry->seq >= pcb->snd.una) {
            break;
        }
        entry = queue_pop(&pcb->queue);
        debugf("remove, seq=%u, len=%u", entry->seq, entry->len);
        memory_free(entry);
    }
    return;
}

static void
tcp_retransmit_queue_emit(void *arg, void *data)
{
    struct tcp_pcb *pcb;
    struct tcp_queue_entry *entry;
    struct timeval now, diff, timeout;

    pcb = (struct tcp_pcb *)arg;
    entry = (struct tcp_queue_entry *)data;
    gettimeofday(&now, 0);
    timersub(&now, &entry->first, &diff);
    if (diff.tv_sec >= TCP_RETRANSMIT_DEADLINE) {
        pcb->state = TCP_PCB_STATE_CLOSED;
        sched_wakeup(&pcb->ctx);
        return;
    }
    timeout = entry->last;
    timeval_add_usec(&timeout, entry->rto);
    if (timercmp(&now, &timeout, >)) {
        tcp_output_segment(entry->seq, pcb->rcv.nxt, entry->flg, pcb->rcv.wnd, entry->data, entry->len, &pcb->local, &pcb->foreign);
        entry->last = now;
        entry->rto *= 2;
    }
}

static int
tcp_output(struct tcp_pcb *pcb, uint8 flg, uint8 *data, uint len)
{
    uint32 seq;

    seq = pcb->snd.nxt;
    if (TCP_FLG_ISSET(flg, TCP_FLG_SYN)) {
        seq = pcb->iss;
    }
    if (TCP_FLG_ISSET(flg, TCP_FLG_SYN | TCP_FLG_FIN) || len) {
        tcp_retransmit_queue_add(pcb, seq, flg, data, len);
    }
    return tcp_output_segment(seq, pcb->rcv.nxt, flg, pcb->rcv.wnd, data, len, &pcb->local, &pcb->foreign);
}

/* rfc793 - section 3.9 [Event Processing > SEGMENT ARRIVES] */
static void
tcp_segment_arrives(struct tcp_segment_info *seg, uint8 flags, uint8 *data, uint len, struct ip_endpoint *local, struct ip_endpoint *foreign)
{
    int acceptable = 0;
    struct tcp_pcb *pcb, *new_pcb;

    pcb = tcp_pcb_select(local, foreign);
    if (!pcb || pcb->state == TCP_PCB_STATE_CLOSED) {
        if (TCP_FLG_ISSET(flags, TCP_FLG_RST)) {
            return;
        }
        if (!TCP_FLG_ISSET(flags, TCP_FLG_ACK)) {
            tcp_output_segment(0, seg->seq + seg->len, TCP_FLG_RST | TCP_FLG_ACK, 0, 0, 0, local, foreign);
        } else {
            tcp_output_segment(seg->ack, 0, TCP_FLG_RST, 0, 0, 0, local, foreign);
        }
        return;
    }
    switch(pcb->state) {
    case TCP_PCB_STATE_LISTEN:
        if (TCP_FLG_ISSET(flags, TCP_FLG_RST)) {
            return;
        }
        if (TCP_FLG_ISSET(flags, TCP_FLG_ACK)) {
            tcp_output_segment(seg->ack, 0, TCP_FLG_RST, 0, 0, 0, local, foreign);
            return;
        }
        if (TCP_FLG_ISSET(flags, TCP_FLG_SYN)) {
            if (pcb->mode == TCP_PCB_MODE_SOCKET) {
                new_pcb = tcp_pcb_alloc();
                if (!new_pcb) {
                    errorf("tcp_pcb_alloc() failure");
                    return;
                }
                new_pcb->mode = TCP_PCB_MODE_SOCKET;
                new_pcb->parent = pcb;
                pcb = new_pcb;
            }
            pcb->local = *local;
            pcb->foreign = *foreign;
            pcb->rcv.wnd = sizeof(pcb->buf);
            pcb->rcv.nxt = seg->seq + 1;
            pcb->irs = seg->seq;
            pcb->iss = random();
            tcp_output(pcb, TCP_FLG_SYN | TCP_FLG_ACK, 0, 0);
            pcb->snd.nxt = pcb->iss + 1;
            pcb->snd.una = pcb->iss;
            pcb->state = TCP_PCB_STATE_SYN_RECEIVED;
            return;
        }
        return;
    case TCP_PCB_STATE_SYN_SENT:
        if (TCP_FLG_ISSET(flags, TCP_FLG_ACK)) {
            if (seg->ack <= pcb->iss || seg->ack > pcb->snd.nxt) {
                tcp_output_segment(seg->ack, 0, TCP_FLG_RST, 0, 0, 0, local, foreign);
                return;
            }
            if (pcb->snd.una <= seg->ack && seg->ack <= pcb->snd.nxt) {
                acceptable = 1;
            }
        }
        if (TCP_FLG_ISSET(flags, TCP_FLG_SYN)) {
            pcb->rcv.nxt = seg->seq + 1;
            pcb->irs = seg->seq;
            if (acceptable) {
                pcb->snd.una = seg->ack;
                tcp_retransmit_queue_cleanup(pcb);
            }
            if (pcb->snd.una > pcb->iss) {
                pcb->state = TCP_PCB_STATE_ESTABLISHED;
                tcp_output(pcb, TCP_FLG_ACK, 0, 0);
                pcb->snd.wnd = seg->wnd;
                pcb->snd.wl1 = seg->seq;
                pcb->snd.wl2 = seg->ack;
                sched_wakeup(&pcb->ctx);
                return;
            } else {
                pcb->state = TCP_PCB_STATE_SYN_RECEIVED;
                tcp_output(pcb, TCP_FLG_SYN | TCP_FLG_ACK, 0, 0);
                return;
            }
        }
        return;
    }
    /* Otherwise */

    /* 1st check sequence number */
    switch (pcb->state) {
    case TCP_PCB_STATE_SYN_RECEIVED:
    case TCP_PCB_STATE_ESTABLISHED:
    case TCP_PCB_STATE_FIN_WAIT1:
    case TCP_PCB_STATE_FIN_WAIT2:
    case TCP_PCB_STATE_CLOSE_WAIT:
    case TCP_PCB_STATE_LAST_ACK:
        if (!seg->len) {
            if (!pcb->rcv.wnd) {
                if (seg->seq == pcb->rcv.nxt) {
                    acceptable = 1;
                }
            } else {
                if (pcb->rcv.nxt <= seg->seq && seg->seq < pcb->rcv.nxt + pcb->rcv.wnd) {
                    acceptable = 1;
                }
            }
        } else {
            if (!pcb->rcv.wnd) {
                /* not acceptable */
            } else {
                if ((pcb->rcv.nxt <= seg->seq && seg->seq < pcb->rcv.nxt + pcb->rcv.wnd) ||
                    (pcb->rcv.nxt <= seg->seq + seg->len - 1 && seg->seq + seg->len - 1 < pcb->rcv.nxt + pcb->rcv.wnd)) {
                    acceptable = 1;
                }
            }
        }
        if (!acceptable) {
            if (!TCP_FLG_ISSET(flags, TCP_FLG_RST)) {
                tcp_output(pcb, TCP_FLG_ACK, 0, 0);
            }
            return;
        }
    }
    /* 5th check the ACK field */
    if (!TCP_FLG_ISSET(flags, TCP_FLG_ACK)) {
        return;
    }
    switch (pcb->state) {
    case TCP_PCB_STATE_SYN_RECEIVED:
        if (pcb->snd.una <= seg->ack && seg->ack <= pcb->snd.nxt) {
            pcb->state = TCP_PCB_STATE_ESTABLISHED;
            sched_wakeup(&pcb->ctx);
            if (pcb->parent) {
                queue_push(&pcb->parent->backlog, pcb);
                sched_wakeup(&pcb->parent->ctx);
            }
        } else {
            tcp_output_segment(seg->ack, 0, TCP_FLG_RST, 0, 0, 0, local, foreign);
            return;
        }
        /* fall through */
    case TCP_PCB_STATE_ESTABLISHED:
    case TCP_PCB_STATE_FIN_WAIT1:
    case TCP_PCB_STATE_FIN_WAIT2:
    case TCP_PCB_STATE_CLOSE_WAIT:
        if (pcb->snd.una < seg->ack && seg->ack <= pcb->snd.nxt) {
            pcb->snd.una = seg->ack;
            tcp_retransmit_queue_cleanup(pcb);
            if (pcb->snd.wl1 < seg->seq || (pcb->snd.wl1 == seg->seq && pcb->snd.wl2 <= seg->ack)) {
                pcb->snd.wnd = seg->wnd;
                pcb->snd.wl1 = seg->seq;
                pcb->snd.wl2 = seg->ack;
            }
        } else if (seg->ack < pcb->snd.una) {
            /* ignore */
        } else if (seg->ack > pcb->snd.nxt) {
            tcp_output(pcb, TCP_FLG_ACK, 0, 0);
            return;
        }
        switch (pcb->state) {
        case TCP_PCB_STATE_FIN_WAIT1:
            if (seg->ack == pcb->snd.nxt) {
                pcb->state = TCP_PCB_STATE_FIN_WAIT2;
            }
            break;
        case TCP_PCB_STATE_FIN_WAIT2:
            break;
        case TCP_PCB_STATE_CLOSE_WAIT:
            break;
        }
        break;
    case TCP_PCB_STATE_LAST_ACK:
        if (seg->ack == pcb->snd.nxt) {
            pcb->state = TCP_PCB_STATE_CLOSED;
            tcp_pcb_release(pcb);
        }
        return;
    }
    /* 7th, process the segment text */
    switch (pcb->state) {
    case TCP_PCB_STATE_ESTABLISHED:
    case TCP_PCB_STATE_FIN_WAIT1:
    case TCP_PCB_STATE_FIN_WAIT2:
        if (len) {
            memmove(pcb->buf + (sizeof(pcb->buf) - pcb->rcv.wnd), data, len);
            pcb->rcv.nxt = seg->seq + seg->len;
            pcb->rcv.wnd -= len;
            tcp_output(pcb, TCP_FLG_ACK, 0, 0);
            sched_wakeup(&pcb->ctx);
        }
        break;
    case TCP_PCB_STATE_CLOSE_WAIT:
    case TCP_PCB_STATE_LAST_ACK:
        break;
    }
    /* 8th, check the FIN bit */
    if (TCP_FLG_ISSET(flags, TCP_FLG_FIN)) {
        switch (pcb->state) {
        case TCP_PCB_STATE_CLOSED:
        case TCP_PCB_STATE_LISTEN:
            return;
        }
        pcb->rcv.nxt = seg->seq + 1;
        tcp_output(pcb, TCP_FLG_ACK, 0, 0);
        switch (pcb->state) {
        case TCP_PCB_STATE_SYN_RECEIVED:
        case TCP_PCB_STATE_ESTABLISHED:
            pcb->state = TCP_PCB_STATE_CLOSE_WAIT;
            sched_wakeup(&pcb->ctx);
            break;
        case TCP_PCB_STATE_FIN_WAIT1:
            if (seg->ack == pcb->snd.nxt) {
                pcb->state = TCP_PCB_STATE_TIME_WAIT;
            } else {
                pcb->state = TCP_PCB_STATE_CLOSING;
            }
            break;
        case TCP_PCB_STATE_FIN_WAIT2:
            pcb->state = TCP_PCB_STATE_TIME_WAIT;
            break;
        case TCP_PCB_STATE_CLOSE_WAIT:
            break;
        case TCP_PCB_STATE_LAST_ACK:
            break;
        }
    }

    return;
}

static void
tcp_input(const uint8 *data, uint len, ip_addr_t src, ip_addr_t dst, struct ip_iface *iface)
{
    struct tcp_hdr *hdr;
    struct pseudo_hdr pseudo;
    uint16 psum;
    char addr1[IP_ADDR_STR_LEN];
    char addr2[IP_ADDR_STR_LEN];
    struct ip_endpoint local, foreign;
    uint16 hlen;
    struct tcp_segment_info seg;

    if (len < sizeof(*hdr)) {
        errorf("too short");
        return;
    }
    hdr = (struct tcp_hdr *)data;
    pseudo.src = src;
    pseudo.dst = dst;
    pseudo.zero = 0;
    pseudo.protocol = IP_PROTOCOL_TCP;
    pseudo.len = hton16(len);
    psum = ~cksum16((uint16 *)&pseudo, sizeof(pseudo), 0);
    if (cksum16((uint16 *)hdr, len, psum) != 0) {
        errorf("checksum error");
        return;
    }
    if (src == IP_ADDR_BROADCAST || src == iface->broadcast || dst == IP_ADDR_BROADCAST || dst == iface->broadcast) {
        errorf("only supports unicast, src=%s, dst=%s",
            ip_addr_ntop(src, addr1, sizeof(addr1)), ip_addr_ntop(dst, addr2, sizeof(addr2)));
        return;
    }
    debugf("%s:%d => %s:%d, len=%u (payload=%u)",
        ip_addr_ntop(src, addr1, sizeof(addr1)), ntoh16(hdr->src),
        ip_addr_ntop(dst, addr2, sizeof(addr2)), ntoh16(hdr->dst),
        len, len - sizeof(*hdr));
    local.addr = dst;
    local.port = hdr->dst;
    foreign.addr = src;
    foreign.port = hdr->src;
    hlen = (hdr->off >> 4) << 2;
    seg.seq = ntoh32(hdr->seq);
    seg.ack = ntoh32(hdr->ack);
    seg.len = len - hlen;
    if (TCP_FLG_ISSET(hdr->flg, TCP_FLG_SYN)) {
        seg.len++;
    }
    if (TCP_FLG_ISSET(hdr->flg, TCP_FLG_FIN)) {
        seg.len++;
    }
    seg.wnd = ntoh16(hdr->wnd);
    seg.up = ntoh16(hdr->up);
    mutex_lock(&mutex);
    tcp_segment_arrives(&seg, hdr->flg, (uint8 *)hdr + hlen, len - hlen, &local, &foreign);
    mutex_unlock(&mutex);
    return;
}

static void
tcp_timer(void)
{
    struct tcp_pcb *pcb;

    mutex_lock(&mutex);
    for (pcb = pcbs; pcb < tailof(pcbs); pcb++) {
        if (pcb->state == TCP_PCB_STATE_FREE) {
            continue;
        }
        queue_foreach(&pcb->queue, tcp_retransmit_queue_emit, pcb);
    }
    mutex_unlock(&mutex);

}

static void
event_handler(void *arg)
{
    struct tcp_pcb *pcb;

    mutex_lock(&mutex);
    for (pcb = pcbs; pcb < tailof(pcbs); pcb++) {
        if (pcb->state != TCP_PCB_STATE_FREE) {
            sched_interrupt(&pcb->ctx);
        }
    }
    mutex_unlock(&mutex);
}

int
tcp_init(void)
{
    struct timeval interval = {0, 100000}; /* 100ms */

    if (ip_protocol_register(IP_PROTOCOL_TCP, tcp_input) == -1) {
        errorf("ip_protocol_register() failure");
        return -1;
    }
    if (net_timer_register(interval, tcp_timer) == -1) {
        errorf("net_timer_register() failure");
        return -1;
    }
    net_event_subscribe(event_handler, 0);
    return 0;
}

/*
 * TCP User Command (RFC793)
 */

int
tcp_open_rfc793(struct ip_endpoint *local, struct ip_endpoint *foreign, int active)
{
    struct tcp_pcb *pcb;
    char ep1[IP_ENDPOINT_STR_LEN];
    char ep2[IP_ENDPOINT_STR_LEN];
    int state, id;

    mutex_lock(&mutex);
    pcb = tcp_pcb_alloc();
    if (!pcb) {
        errorf("tcp_pcb_alloc() failure");
        mutex_unlock(&mutex);
        return -1;
    }
    if (active) {
        debugf("active open: local=%s, foreign=%s, connecting...",
            ip_endpoint_ntop(local, ep1, sizeof(ep1)), ip_endpoint_ntop(foreign, ep2, sizeof(ep2)));
        pcb->local = *local;
        pcb->foreign = *foreign;
        pcb->rcv.wnd = sizeof(pcb->buf);
        pcb->iss = random();
        if (tcp_output(pcb, TCP_FLG_SYN, 0, 0) == -1) {
            errorf("tcp_output() failure");
            pcb->state = TCP_PCB_STATE_CLOSED;
            tcp_pcb_release(pcb);
            mutex_unlock(&mutex);
            return -1;
        }
        pcb->snd.una = pcb->iss;
        pcb->snd.nxt = pcb->iss + 1;
        pcb->state = TCP_PCB_STATE_SYN_SENT;
    } else {
        debugf("passive open: local=%s, waiting for connection...", ip_endpoint_ntop(local, ep1, sizeof(ep1)));
        pcb->local = *local;
        if (foreign) {
            pcb->foreign = *foreign;
        }
        pcb->state = TCP_PCB_STATE_LISTEN;
    }
AGAIN:
    state = pcb->state;
    while (pcb->state == state) {
        if (sched_sleep(&pcb->ctx, &mutex, 0) == -1) {
            debugf("interrupted");
            pcb->state = TCP_PCB_STATE_CLOSED;
            tcp_pcb_release(pcb);
            mutex_unlock(&mutex);
            net_errno = EINTR;
            return -1;
        }
    }
    if (pcb->state != TCP_PCB_STATE_ESTABLISHED) {
        if (pcb->state == TCP_PCB_STATE_SYN_RECEIVED) {
            goto AGAIN;
        }
        errorf("open error: %d", pcb->state);
        pcb->state = TCP_PCB_STATE_CLOSED;
        tcp_pcb_release(pcb);
        mutex_unlock(&mutex);
        return -1;
    }
    id = tcp_pcb_id(pcb);
    debugf("connection established: local=%s, foreign=%s",
        ip_endpoint_ntop(&pcb->local, ep1, sizeof(ep1)), ip_endpoint_ntop(&pcb->foreign, ep2, sizeof(ep2)));
    mutex_unlock(&mutex);
    return id;
}

int
tcp_close(int id)
{
    struct tcp_pcb *pcb;

    mutex_lock(&mutex);
    pcb = tcp_pcb_get(id);
    if (!pcb) {
        errorf("pcb not found");
        mutex_unlock(&mutex);
        return -1;
    }
    switch (pcb->state) {
    case TCP_PCB_STATE_LISTEN:
        pcb->state = TCP_PCB_STATE_CLOSED;
        break;
    case TCP_PCB_STATE_ESTABLISHED:
        tcp_output(pcb, TCP_FLG_ACK | TCP_FLG_FIN, 0, 0);
        pcb->snd.nxt++;
        pcb->state = TCP_PCB_STATE_FIN_WAIT1;
        break;
    case TCP_PCB_STATE_CLOSE_WAIT:
        tcp_output(pcb, TCP_FLG_ACK | TCP_FLG_FIN, 0, 0);
        pcb->snd.nxt++;
        pcb->state = TCP_PCB_STATE_LAST_ACK;
        break;
    default:
        errorf("unknown state '%u'", pcb->state);
        mutex_unlock(&mutex);
        return -1;
    }
    if (pcb->state == TCP_PCB_STATE_CLOSED) {
        tcp_pcb_release(pcb);
    } else {
        sched_wakeup(&pcb->ctx);
    }
    mutex_unlock(&mutex);
    return 0;
}

/*
 * TCP User Command (Socket)
 */

int
tcp_open(void)
{
    struct tcp_pcb *pcb;
    int id;

    mutex_lock(&mutex);
    pcb = tcp_pcb_alloc();
    if (!pcb) {
        errorf("tcp_pcb_alloc() failure");
        mutex_unlock(&mutex);
        return -1;
    }
    pcb->mode = TCP_PCB_MODE_SOCKET;
    id = tcp_pcb_id(pcb);
    mutex_unlock(&mutex);
    return id;
}

int
tcp_connect(int id, struct ip_endpoint *foreign)
{
    struct tcp_pcb *pcb;
    struct ip_endpoint local;
    struct ip_iface *iface;
    char addr[IP_ADDR_STR_LEN];
    int p;
    int state;

    mutex_lock(&mutex);
    pcb = tcp_pcb_get(id);
    if (!pcb) {
        errorf("pcb not found");
        mutex_unlock(&mutex);
        return -1;
    }
    if (pcb->mode != TCP_PCB_MODE_SOCKET) {
        errorf("not opened in socket mode");
        mutex_unlock(&mutex);
        return -1;
    }
    local.addr = pcb->local.addr;
    local.port = pcb->local.port;
    if (local.addr == IP_ADDR_ANY) {
        iface = ip_route_get_iface(foreign->addr);
        if (!iface) {
            errorf("ip_route_get_iface() failure");
            mutex_unlock(&mutex);
            return -1;
        }
        debugf("select source address: %s", ip_addr_ntop(iface->unicast, addr, sizeof(addr)));
        local.addr = iface->unicast;
    }
    if (!local.port) {
        for (p = TCP_SOURCE_PORT_MIN; p <= TCP_SOURCE_PORT_MAX; p++) {
            local.port = p;
            if (!tcp_pcb_select(&local, foreign)) {
                debugf("dynamic assign source port: %d", ntoh16(local.port));
                pcb->local.port = local.port;
                break;
            }
        }
        if (!local.port) {
            debugf("failed to dynamic assign source port");
            mutex_unlock(&mutex);
            return -1;
        }
    }
    pcb->local.addr = local.addr;
    pcb->local.port = local.port;
    pcb->foreign.addr = foreign->addr;
    pcb->foreign.port = foreign->port;
    pcb->rcv.wnd = sizeof(pcb->buf);
    pcb->iss = random();
    if (tcp_output(pcb, TCP_FLG_SYN, 0, 0) == -1) {
        errorf("tcp_output() failure");
        pcb->state = TCP_PCB_STATE_CLOSED;
        tcp_pcb_release(pcb);
        mutex_unlock(&mutex);
        return -1;
    }
    pcb->snd.una = pcb->iss;
    pcb->snd.nxt = pcb->iss + 1;
    pcb->state = TCP_PCB_STATE_SYN_SENT;
AGAIN:
    state = pcb->state;
    while (pcb->state == state) {
        if (sched_sleep(&pcb->ctx, &mutex, 0) == -1) {
            debugf("interrupted");
            pcb->state = TCP_PCB_STATE_CLOSED;
            tcp_pcb_release(pcb);
            mutex_unlock(&mutex);
            net_errno = EINTR;
            return -1;
        }
    }
    if (pcb->state != TCP_PCB_STATE_ESTABLISHED) {
        if (pcb->state == TCP_PCB_STATE_SYN_RECEIVED) {
            goto AGAIN;
        }
        errorf("open error: %d", pcb->state);
        pcb->state = TCP_PCB_STATE_CLOSED;
        tcp_pcb_release(pcb);
        mutex_unlock(&mutex);
        return -1;
    }
    id = tcp_pcb_id(pcb);
    mutex_unlock(&mutex);
    return id;
}

int
tcp_bind(int id, struct ip_endpoint *local)
{
    struct tcp_pcb *pcb, *exist;
    char ep[IP_ENDPOINT_STR_LEN];

    mutex_lock(&mutex);
    pcb = tcp_pcb_get(id);
    if (!pcb) {
        errorf("pcb not found");
        mutex_unlock(&mutex);
        return -1;
    }
    if (pcb->mode != TCP_PCB_MODE_SOCKET) {
        errorf("not opened in socket mode");
        mutex_unlock(&mutex);
        return -1;
    }
    exist = tcp_pcb_select(local, 0);
    if (exist) {
        errorf("already bound, exist=%s", ip_endpoint_ntop(&exist->local, ep, sizeof(ep)));
        mutex_unlock(&mutex);
        return -1;
    }
    pcb->local = *local;
    debugf("success: local=%s", ip_endpoint_ntop(&pcb->local, ep, sizeof(ep)));
    mutex_unlock(&mutex);
    return 0;
}

int
tcp_listen(int id, int backlog)
{
    struct tcp_pcb *pcb;

    mutex_lock(&mutex);
    pcb = tcp_pcb_get(id);
    if (!pcb) {
        errorf("pcb not found");
        mutex_unlock(&mutex);
        return -1;
    }
    if (pcb->mode != TCP_PCB_MODE_SOCKET) {
        errorf("not opened in socket mode");
        mutex_unlock(&mutex);
        return -1;
    }
    pcb->state = TCP_PCB_STATE_LISTEN;
    (void)backlog; /* TODO: set backlog */
    mutex_unlock(&mutex);
    return 0;
}

int
tcp_accept(int id, struct ip_endpoint *foreign)
{
    struct tcp_pcb *pcb, *new_pcb;
    int new_id;

    mutex_lock(&mutex);
    pcb = tcp_pcb_get(id);
    if (!pcb) {
        errorf("pcb not found");
        mutex_unlock(&mutex);
        return -1;
    }
    if (pcb->mode != TCP_PCB_MODE_SOCKET) {
        errorf("not opened in socket mode");
        mutex_unlock(&mutex);
        return -1;
    }
    if (pcb->state != TCP_PCB_STATE_LISTEN) {
        errorf("not in LISTEN state");
        mutex_unlock(&mutex);
        return -1;
    }
    while (!(new_pcb = queue_pop(&pcb->backlog))) {
        if (sched_sleep(&pcb->ctx, &mutex, 0) == -1) {
            debugf("interrupted");
            mutex_unlock(&mutex);
            net_errno = EINTR;
            return -1;
        }
        if (pcb->state == TCP_PCB_STATE_CLOSED) {
            debugf("closed");
            tcp_pcb_release(pcb);
            mutex_unlock(&mutex);
            return -1;
        }
    }
    if (foreign) {
        *foreign = new_pcb->foreign;
    }
    new_id = tcp_pcb_id(new_pcb);
    mutex_unlock(&mutex);
    return new_id;
}

int
tcp_send(int id, uint8 *data, uint len)
{
    struct tcp_pcb *pcb;
    int sent = 0;
    struct ip_iface *iface;
    uint mss, cap, slen;

    mutex_lock(&mutex);
    pcb = tcp_pcb_get(id);
    if (!pcb) {
        errorf("pcb not found");
        mutex_unlock(&mutex);
        return -1;
    }
RETRY:
    switch (pcb->state) {
    case TCP_PCB_STATE_ESTABLISHED:
    case TCP_PCB_STATE_CLOSE_WAIT:
        iface = ip_route_get_iface(pcb->foreign.addr);
        if (!iface) {
            errorf("iface not found");
            mutex_unlock(&mutex);
            return -1;
        }
        mss = NET_IFACE(iface)->dev->mtu - (IP_HDR_SIZE_MIN + sizeof(struct tcp_hdr));
        while (sent < (int)len) {
            cap = pcb->snd.wnd - (pcb->snd.nxt - pcb->snd.una);
            if (!cap) {
                if (sched_sleep(&pcb->ctx, &mutex, 0) == -1) {
                    debugf("interrupted");
                    if (!sent) {
                        mutex_unlock(&mutex);
                        net_errno = EINTR;
                        return -1;
                    }
                    break;
                }
                goto RETRY;
            }
            slen = MIN(MIN(mss, len - sent), cap);
            if (tcp_output(pcb, TCP_FLG_ACK | TCP_FLG_PSH, data + sent, slen) == -1) {
                errorf("tcp_output() failure");
                pcb->state = TCP_PCB_STATE_CLOSED;
                tcp_pcb_release(pcb);
                mutex_unlock(&mutex);
                return -1;
            }
            pcb->snd.nxt += slen;
            sent += slen;
        }
        break;
    case TCP_PCB_STATE_LAST_ACK:
        errorf("connection closing");
        mutex_unlock(&mutex);
        return -1;
    default:
        errorf("unknown state '%u'", pcb->state);
        mutex_unlock(&mutex);
        return -1;
    }
    mutex_unlock(&mutex);
    return sent;
}

int
tcp_receive(int id, uint8 *buf, uint size)
{
    struct tcp_pcb *pcb;
    uint remain, len;

    mutex_lock(&mutex);
    pcb = tcp_pcb_get(id);
    if (!pcb) {
        errorf("pcb not found");
        mutex_unlock(&mutex);
        return -1;
    }
RETRY:
    switch (pcb->state) {
    case TCP_PCB_STATE_ESTABLISHED:
        remain = sizeof(pcb->buf) - pcb->rcv.wnd;
        if (!remain) {
            if (sched_sleep(&pcb->ctx, &mutex, 0) == -1) {
                debugf("interrupted");
                mutex_unlock(&mutex);
                net_errno = EINTR;
                return -1;
            }
            goto RETRY;
        }
        break;
    case TCP_PCB_STATE_CLOSE_WAIT:
        remain = sizeof(pcb->buf) - pcb->rcv.wnd;
        if (remain) {
            break;
        }
        debugf("connection closing");
        mutex_unlock(&mutex);
        return 0;
    default:
        errorf("unknown state '%u'", pcb->state);
        mutex_unlock(&mutex);
        return -1;
    }
    len = MIN(size, remain);
    memmove(buf, pcb->buf, len);
    memmove(pcb->buf, pcb->buf + len, remain - len);
    pcb->rcv.wnd += len;
    mutex_unlock(&mutex);
    return len;
}

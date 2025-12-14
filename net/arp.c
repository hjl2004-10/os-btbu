/* ch9: 网络协议栈 - ARP层实现 */
#include "platform.h"
#include "util.h"
#include "net.h"
#include "ether.h"
#include "arp.h"
#include "ip.h"

/* see https://www.iana.org/assignments/arp-parameters/arp-parameters.txt */
#define ARP_HRD_ETHER 0x0001
/* NOTE: use same value as the Ethernet types */
#define ARP_PRO_IP ETHER_TYPE_IP

#define ARP_OP_REQUEST 1
#define ARP_OP_REPLY   2

#define ARP_CACHE_SIZE 32
#define ARP_CACHE_TIMEOUT 30 /* seconds */

#define ARP_CACHE_STATE_FREE       0
#define ARP_CACHE_STATE_INCOMPLETE 1
#define ARP_CACHE_STATE_RESOLVED   2
#define ARP_CACHE_STATE_STATIC     3

struct arp_hdr {
    uint16 hrd;
    uint16 pro;
    uint8 hln;
    uint8 pln;
    uint16 op;
};

struct arp_ether_ip {
    struct arp_hdr hdr;
    uint8 sha[ETHER_ADDR_LEN];
    uint8 spa[IP_ADDR_LEN];
    uint8 tha[ETHER_ADDR_LEN];
    uint8 tpa[IP_ADDR_LEN];
};

struct arp_cache {
    unsigned char state;
    ip_addr_t pa;
    uint8 ha[ETHER_ADDR_LEN];
    struct timeval timestamp;
};

static mutex_t mutex = MUTEX_INITIALIZER;
static struct arp_cache caches[ARP_CACHE_SIZE];

/*
 * ARP Cache
 *
 * NOTE: ARP Cache functions must be called after mutex locked
 */

static void
arp_cache_delete(struct arp_cache *cache)
{
    char addr1[IP_ADDR_STR_LEN] __attribute__((unused));
    char addr2[ETHER_ADDR_STR_LEN] __attribute__((unused));

    debugf("DELETE: pa=%s, ha=%s", ip_addr_ntop(cache->pa, addr1, sizeof(addr1)), ether_addr_ntop(cache->ha, addr2, sizeof(addr2)));
    cache->state = ARP_CACHE_STATE_FREE;
    cache->pa = 0;
    memset(cache->ha, 0, ETHER_ADDR_LEN);
    timerclear(&cache->timestamp);
}

static struct arp_cache *
arp_cache_alloc(void)
{
    struct arp_cache *entry, *oldest = 0;

    for (entry = caches; entry < tailof(caches); entry++) {
        if (entry->state == ARP_CACHE_STATE_FREE) {
            return entry;
        }
        if (!oldest || timercmp(&oldest->timestamp, &entry->timestamp, >)) {
            oldest = entry;
        }
    }
    arp_cache_delete(oldest);
    return oldest;
}

static struct arp_cache *
arp_cache_select(ip_addr_t pa)
{
    struct arp_cache *entry;

    for (entry = caches; entry < tailof(caches); entry++) {
        if (entry->state != ARP_CACHE_STATE_FREE && entry->pa == pa) {
            return entry;
        }
    }
    return 0;
}

static struct arp_cache *
arp_cache_update(ip_addr_t pa, const uint8 *ha)
{
    struct arp_cache *cache;
    char addr1[IP_ADDR_STR_LEN] __attribute__((unused));
    char addr2[ETHER_ADDR_STR_LEN] __attribute__((unused));

    cache = arp_cache_select(pa);
    if (!cache) {
        /* not found */
        return 0;
    }
    cache->state = ARP_CACHE_STATE_RESOLVED;
    memmove(cache->ha, ha, ETHER_ADDR_LEN);
    gettimeofday(&cache->timestamp, 0);
    debugf("UPDATE: pa=%s, ha=%s", ip_addr_ntop(pa, addr1, sizeof(addr1)), ether_addr_ntop(ha, addr2, sizeof(addr2)));
    return cache;
}

static struct arp_cache *
arp_cache_insert(ip_addr_t pa, const uint8 *ha)
{
    struct arp_cache *cache;
    char addr1[IP_ADDR_STR_LEN] __attribute__((unused));
    char addr2[ETHER_ADDR_STR_LEN] __attribute__((unused));

    cache = arp_cache_alloc();
    if (!cache) {
        errorf("arp_cache_alloc() failure");
        return 0;
    }
    cache->state = ARP_CACHE_STATE_RESOLVED;
    cache->pa = pa;
    memmove(cache->ha, ha, ETHER_ADDR_LEN);
    gettimeofday(&cache->timestamp, 0);
    debugf("INSERT: pa=%s, ha=%s", ip_addr_ntop(pa, addr1, sizeof(addr1)), ether_addr_ntop(ha, addr2, sizeof(addr2)));
    return cache;
}

static int
arp_request(struct net_iface *iface, ip_addr_t tpa)
{
    struct arp_ether_ip request;

    request.hdr.hrd = hton16(ARP_HRD_ETHER);
    request.hdr.pro = hton16(ARP_PRO_IP);
    request.hdr.hln = ETHER_ADDR_LEN;
    request.hdr.pln = IP_ADDR_LEN;
    request.hdr.op = hton16(ARP_OP_REQUEST);
    memmove(request.sha, iface->dev->addr, ETHER_ADDR_LEN);
    memmove(request.spa, &((struct ip_iface *)iface)->unicast, IP_ADDR_LEN);
    memset(request.tha, 0, ETHER_ADDR_LEN);
    memmove(request.tpa, &tpa, IP_ADDR_LEN);
    debugf("dev=%s, len=%d", iface->dev->name, sizeof(request));
    return net_device_output(iface->dev, ETHER_TYPE_ARP, (uint8 *)&request, sizeof(request), iface->dev->broadcast);
}

static int
arp_reply(struct net_iface *iface, const uint8 *tha, ip_addr_t tpa, const uint8 *dst)
{
    struct arp_ether_ip reply;

    reply.hdr.hrd = hton16(ARP_HRD_ETHER);
    reply.hdr.pro = hton16(ARP_PRO_IP);
    reply.hdr.hln = ETHER_ADDR_LEN;
    reply.hdr.pln = IP_ADDR_LEN;
    reply.hdr.op = hton16(ARP_OP_REPLY);
    memmove(reply.sha, iface->dev->addr, ETHER_ADDR_LEN);
    memmove(reply.spa, &((struct ip_iface *)iface)->unicast, IP_ADDR_LEN);
    memmove(reply.tha, tha, ETHER_ADDR_LEN);
    memmove(reply.tpa, &tpa, IP_ADDR_LEN);
    debugf("dev=%s, len=%d", iface->dev->name, sizeof(reply));
    return net_device_output(iface->dev, ETHER_TYPE_ARP, (uint8 *)&reply, sizeof(reply), dst);
}

static void
arp_input(const uint8 *data, uint len, struct net_device *dev)
{
    struct arp_ether_ip *msg;
    ip_addr_t spa, tpa;
    int marge = 0;
    struct net_iface *iface;

    if (len < sizeof(*msg)) {
        errorf("too short");
        return;
    }
    msg = (struct arp_ether_ip *)data;
    if (ntoh16(msg->hdr.hrd) != ARP_HRD_ETHER || msg->hdr.hln != ETHER_ADDR_LEN) {
        errorf("unsupported hardware address");
        return;
    }
    if (ntoh16(msg->hdr.pro) != ARP_PRO_IP || msg->hdr.pln != IP_ADDR_LEN) {
        errorf("unsupported protocol address");
        return;
    }
    debugf("dev=%s, len=%u", dev->name, len);
    memmove(&spa, msg->spa, sizeof(spa));
    memmove(&tpa, msg->tpa, sizeof(tpa));
    mutex_lock(&mutex);
    if (arp_cache_update(spa, msg->sha)) {
        /* updated */
        marge = 1;
    }
    mutex_unlock(&mutex);
    iface = net_device_get_iface(dev, NET_IFACE_FAMILY_IP);
    if (iface && ((struct ip_iface *)iface)->unicast == tpa) {
        if (!marge) {
            mutex_lock(&mutex);
            arp_cache_insert(spa, msg->sha);
            mutex_unlock(&mutex);
        }
        if (ntoh16(msg->hdr.op) == ARP_OP_REQUEST) {
            arp_reply(iface, msg->sha, spa, msg->sha);
        }
    }
}

int
arp_resolve(struct net_iface *iface, ip_addr_t pa, uint8 *ha)
{
    struct arp_cache *cache;
    char addr1[IP_ADDR_STR_LEN] __attribute__((unused));
    char addr2[ETHER_ADDR_STR_LEN] __attribute__((unused));

    if (iface->dev->type != NET_DEVICE_TYPE_ETHERNET) {
        debugf("unsupported hardware address type");
        return ARP_RESOLVE_ERROR;
    }
    if (iface->family != NET_IFACE_FAMILY_IP) {
        debugf("unsupported protocol address type");
        return ARP_RESOLVE_ERROR;
    }
    mutex_lock(&mutex);
    cache = arp_cache_select(pa);
    if (!cache) {
        debugf("cache not found, pa=%s", ip_addr_ntop(pa, addr1, sizeof(addr1)));
        cache = arp_cache_alloc();
        if (!cache) {
            mutex_unlock(&mutex);
            errorf("arp_cache_alloc() failure");
            return ARP_RESOLVE_ERROR;
        }
        cache->state = ARP_CACHE_STATE_INCOMPLETE;
        cache->pa = pa;
        gettimeofday(&cache->timestamp, 0);
        mutex_unlock(&mutex);
        arp_request(iface, pa);
        return ARP_RESOLVE_INCOMPLETE;
    }
    if (cache->state == ARP_CACHE_STATE_INCOMPLETE) {
        mutex_unlock(&mutex);
        arp_request(iface, pa); /* just in case packet loss */
        return ARP_RESOLVE_INCOMPLETE;
    }
    memmove(ha, cache->ha, ETHER_ADDR_LEN);
    mutex_unlock(&mutex);
    debugf("resolved, pa=%s, ha=%s",
        ip_addr_ntop(pa, addr1, sizeof(addr1)), ether_addr_ntop(ha, addr2, sizeof(addr2)));
    return ARP_RESOLVE_FOUND;
}

static void
arp_timer_handler(void)
{
    struct arp_cache *entry;
    struct timeval now, diff;

    mutex_lock(&mutex);
    gettimeofday(&now, 0);
    for (entry = caches; entry < tailof(caches); entry++) {
        if (entry->state != ARP_CACHE_STATE_FREE && entry->state != ARP_CACHE_STATE_STATIC) {
            timersub(&now, &entry->timestamp, &diff);
            if (diff.tv_sec > ARP_CACHE_TIMEOUT) {
                arp_cache_delete(entry);
            }
        }
    }
    mutex_unlock(&mutex);
}

int
arp_init(void)
{
    struct timeval interval = {1, 0}; /* 1s */

    if (net_protocol_register(NET_PROTOCOL_TYPE_ARP, arp_input) == -1) {
        errorf("net_protocol_register() failure");
        return -1;
    }
    if (net_timer_register(interval, arp_timer_handler) == -1) {
        errorf("net_timer_register() failure");
        return -1;
    }
    return 0;
}

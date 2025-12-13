/* ch9: 网络协议栈 - IP层实现 */
#include "platform.h"
#include "util.h"
#include "net.h"
#include "ip.h"
#include "arp.h"

struct ip_hdr {
    uint8 vhl;
    uint8 tos;
    uint16 total;
    uint16 id;
    uint16 offset;
    uint8 ttl;
    uint8 protocol;
    uint16 sum;
    ip_addr_t src;
    ip_addr_t dst;
    uint8 options[];
};

struct ip_protocol {
    struct ip_protocol *next;
    uint8 type;
    void (*handler)(const uint8 *data, uint len, ip_addr_t src, ip_addr_t dst, struct ip_iface *iface);
};

struct ip_route {
    struct ip_route *next;
    ip_addr_t network;
    ip_addr_t netmask;
    ip_addr_t nexthop;
    struct ip_iface *iface;
};

const ip_addr_t IP_ADDR_ANY       = 0x00000000; /* 0.0.0.0 */
const ip_addr_t IP_ADDR_BROADCAST = 0xffffffff; /* 255.255.255.255 */

/* NOTE: if you want to add/delete the entries after net_run(), you need to protect these lists with a mutex. */
static struct ip_iface *ifaces;
static struct ip_protocol *protocols;
static struct ip_route *routes;

int
ip_addr_pton(const char *p, ip_addr_t *n)
{
    char *sp, *ep;
    int idx;
    long ret;

    sp = (char *)p;
    for (idx = 0; idx < 4; idx++) {
        ret = strtol(sp, &ep, 10);
        if (ret < 0 || ret > 255) {
            return -1;
        }
        if (ep == sp) {
            return -1;
        }
        if ((idx == 3 && *ep != '\0') || (idx != 3 && *ep != '.')) {
            return -1;
        }
        ((uint8 *)n)[idx] = ret;
        sp = ep + 1;
    }
    return 0;
}

char *
ip_addr_ntop(ip_addr_t n, char *p, uint size)
{
    uint8 *u8;

    u8 = (uint8 *)&n;
    snprintf(p, size, "%d.%d.%d.%d", u8[0], u8[1], u8[2], u8[3]);
    return p;
}

int
ip_endpoint_pton(const char *p, struct ip_endpoint *n)
{
    char *sep;
    char addr[IP_ADDR_STR_LEN] = {0};
    long port;

    sep = strrchr(p, ':');
    if (!sep) {
        return -1;
    }
    memmove(addr, p, sep - p);
    if (ip_addr_pton(addr, &n->addr) == -1) {
        return -1;
    }
    port = strtol(sep+1, 0, 10);
    if (port <= 0 || port > UINT16_MAX) {
        return -1;
    }
    n->port = hton16(port);
    return 0;
}

char *
ip_endpoint_ntop(const struct ip_endpoint *n, char *p, uint size)
{
    uint offset;

    ip_addr_ntop(n->addr, p, size);
    offset = strlen(p);
    snprintf(p + offset, size - offset, ":%d", ntoh16(n->port));
    return p;
}

/* NOTE: must not be call after net_run() */
static struct ip_route *
ip_route_add(ip_addr_t network, ip_addr_t netmask, ip_addr_t nexthop, struct ip_iface *iface)
{
    struct ip_route *route;
    char addr1[IP_ADDR_STR_LEN];
    char addr2[IP_ADDR_STR_LEN];
    char addr3[IP_ADDR_STR_LEN];
    char addr4[IP_ADDR_STR_LEN];

    route = memory_alloc(sizeof(*route));
    if (!route) {
        errorf("memory_alloc() failure");
        return 0;
    }
    route->network = network;
    route->netmask = netmask;
    route->nexthop = nexthop;
    route->iface = iface;
    route->next = routes;
    routes = route;
    infof("route added: network=%s, netmask=%s, nexthop=%s, iface=%s dev=%s",
        ip_addr_ntop(route->network, addr1, sizeof(addr1)),
        ip_addr_ntop(route->netmask, addr2, sizeof(addr2)),
        ip_addr_ntop(route->nexthop, addr3, sizeof(addr3)),
        ip_addr_ntop(route->iface->unicast, addr4, sizeof(addr4)),
        NET_IFACE(iface)->dev->name
    );
    return route;
}

static struct ip_route *
ip_route_lookup(ip_addr_t dst)
{
    struct ip_route *route, *candidate = 0;

    for (route = routes; route; route = route->next) {
        if ((dst & route->netmask) == route->network) {
            if (!candidate || ntoh32(candidate->netmask) < ntoh32(route->netmask)) {
                candidate = route;
            }
        }
    }
    return candidate;
}

/* NOTE: must not be call after net_run() */
int
ip_route_set_default_gateway(struct ip_iface *iface, const char *gateway)
{
    ip_addr_t gw;

    if (ip_addr_pton(gateway, &gw) == -1) {
        errorf("ip_addr_pton() failure, addr=%s", gateway);
        return -1;
    }
    if (!ip_route_add(IP_ADDR_ANY, IP_ADDR_ANY, gw, iface)) {
        errorf("ip_route_add() failure");
        return -1;
    }
    return 0;
}

struct ip_iface *
ip_route_get_iface(ip_addr_t dst)
{
    struct ip_route *route;

    route = ip_route_lookup(dst);
    if (!route) {
        return 0;
    }
    return route->iface;
}

struct ip_iface *
ip_iface_alloc(const char *unicast, const char *netmask)
{
    struct ip_iface *iface;

    iface = memory_alloc(sizeof(*iface));
    if (!iface) {
        errorf("memory_alloc() failure");
        return 0;
    }
    NET_IFACE(iface)->family = NET_IFACE_FAMILY_IP;
    if (ip_addr_pton(unicast, &iface->unicast) == -1) {
        errorf("ip_addr_pton() failure, addr=%s", unicast);
        memory_free(iface);
        return 0;
    }
    if (ip_addr_pton(netmask, &iface->netmask) == -1) {
        errorf("ip_addr_pton() failure, addr=%s", netmask);
        memory_free(iface);
        return 0;
    }
    iface->broadcast = (iface->unicast & iface->netmask) | ~iface->netmask;
    return iface;
}

/* NOTE: must not be call after net_run() */
int
ip_iface_register(struct net_device *dev, struct ip_iface *iface)
{
    char addr1[IP_ADDR_STR_LEN];
    char addr2[IP_ADDR_STR_LEN];
    char addr3[IP_ADDR_STR_LEN];

    if (net_device_add_iface(dev, NET_IFACE(iface)) == -1) {
        errorf("net_device_add_iface() failure");
        return -1;
    }
    if (!ip_route_add(iface->unicast & iface->netmask, iface->netmask, IP_ADDR_ANY, iface)) {
        errorf("ip_route_add() failure");
        return -1;
    }
    iface->next = ifaces;
    ifaces = iface;
    infof("registered: dev=%s, unicast=%s, netmask=%s, broadcast=%s", dev->name,
        ip_addr_ntop(iface->unicast, addr1, sizeof(addr1)),
        ip_addr_ntop(iface->netmask, addr2, sizeof(addr2)),
        ip_addr_ntop(iface->broadcast, addr3, sizeof(addr3)));
    return 0;
}

int
ip_iface_reconfigure(struct ip_iface *iface, ip_addr_t unicast, ip_addr_t netmask)
{
    struct ip_route *route;

    iface->unicast = unicast;
    iface->netmask = netmask;
    iface->broadcast = (iface->unicast & iface->netmask) | ~iface->netmask;
    for (route = routes; route; route = route->next) {
        if (route->iface == iface) {
            route->network = iface->unicast & iface->netmask;
            route->netmask = iface->netmask;
        }
    }
    return 0;
}

struct ip_iface *
ip_iface_select(ip_addr_t addr)
{
    struct ip_iface *entry;

    for (entry = ifaces; entry; entry = entry->next) {
        if (entry->unicast == addr) {
            break;
        }
    }
    return entry;
}

/* NOTE: must not be call after net_run() */
int
ip_protocol_register(uint8 type, void (*handler)(const uint8 *data, uint len, ip_addr_t src, ip_addr_t dst, struct ip_iface *iface))
{
    struct ip_protocol *entry;

    for (entry = protocols; entry; entry = entry->next) {
        if (entry->type == type) {
            errorf("already exists, type=%u", type);
            return -1;
        }
    }
    entry = memory_alloc(sizeof(*entry));
    if (!entry) {
        errorf("memory_alloc() failure");
        return -1;
    }
    entry->type = type;
    entry->handler = handler;
    entry->next = protocols;
    protocols = entry;
    infof("registered, type=%u", entry->type);
    return 0;
}

static void
ip_input(const uint8 *data, uint len, struct net_device *dev)
{
    struct ip_hdr *hdr;
    uint8 v;
    uint16 hlen, total, offset;
    struct ip_iface *iface;
    char addr[IP_ADDR_STR_LEN];
    struct ip_protocol *proto;

    if (len < IP_HDR_SIZE_MIN) {
        errorf("too short");
        return;
    }
    hdr = (struct ip_hdr *)data;
    v = hdr->vhl >> 4;
    if (v != IP_VERSION_IPV4) {
        errorf("ip version error: v=%u", v);
        return;
    }
    hlen = (hdr->vhl & 0x0f) << 2;
    if (len < hlen) {
        errorf("header length error: len=%u < hlen=%u", len, hlen);
        return;
    }
    total = ntoh16(hdr->total);
    if (len < total) {
        errorf("total length error: len=%u < total=%u", len, total);
        return;
    }
    if (cksum16((uint16 *)hdr, hlen, 0) != 0) {
        errorf("checksum error");
        return;
    }
    offset = ntoh16(hdr->offset);
    if (offset & 0x2000 || offset & 0x1fff) {
        errorf("fragments does not support");
        return;
    }
    iface = (struct ip_iface *)net_device_get_iface(dev, NET_IFACE_FAMILY_IP);
    if (!iface) {
        /* iface is not registered to the device */
        return;
    }
    if (hdr->dst != iface->unicast) {
        if (hdr->dst != iface->broadcast && hdr->dst != IP_ADDR_BROADCAST) {
            /* for other host */
            return;
        }
    }
    debugf("dev=%s, iface=%s, protocol=%u, total=%u",
        dev->name, ip_addr_ntop(iface->unicast, addr, sizeof(addr)), hdr->protocol, total);
    for (proto = protocols; proto; proto = proto->next) {
        if (proto->type == hdr->protocol) {
            proto->handler((uint8 *)hdr + hlen, total - hlen, hdr->src, hdr->dst, iface);
            return;
        }
    }
    /* unsupported protocol */
}

static int
ip_output_device(struct ip_iface *iface, const uint8 *data, uint len, ip_addr_t dst)
{
    uint8 hwaddr[NET_DEVICE_ADDR_LEN] = {0};
    int ret;

    if (NET_IFACE(iface)->dev->flags & NET_DEVICE_FLAG_NEED_ARP) {
        if (dst == iface->broadcast || dst == IP_ADDR_BROADCAST) {
            memmove(hwaddr, NET_IFACE(iface)->dev->broadcast, NET_IFACE(iface)->dev->alen);
        } else {
            ret = arp_resolve(NET_IFACE(iface), dst, hwaddr);
            if (ret != ARP_RESOLVE_FOUND) {
                return ret;
            }
        }
    }
    return net_device_output(NET_IFACE(iface)->dev, NET_PROTOCOL_TYPE_IP, data, len, hwaddr);
}

static int
ip_output_core(struct ip_iface *iface, uint8 protocol, const uint8 *data, uint len, ip_addr_t src, ip_addr_t dst, ip_addr_t nexthop, uint16 id, uint16 offset)
{
    uint8 *buf;
    struct ip_hdr *hdr;
    uint16 hlen, total;
    char addr[IP_ADDR_STR_LEN];
    int ret;

    buf = memory_alloc(IP_TOTAL_SIZE_MAX);
    if (!buf) {
        return -1;
    }
    hdr = (struct ip_hdr *)buf;
    hlen = IP_HDR_SIZE_MIN;
    hdr->vhl = (IP_VERSION_IPV4 << 4) | (hlen >> 2);
    hdr->tos = 0;
    total = hlen + len;
    hdr->total = hton16(total);
    hdr->id = hton16(id);
    hdr->offset = hton16(offset);
    hdr->ttl = 0xff;
    hdr->protocol = protocol;
    hdr->sum = 0;
    hdr->src = src;
    hdr->dst = dst;
    hdr->sum = cksum16((uint16 *)hdr, hlen, 0); /* don't convert byteorder */
    memmove(hdr+1, data, len);
    debugf("dev=%s, dst=%s, protocol=%u, len=%u",
        NET_IFACE(iface)->dev->name, ip_addr_ntop(dst, addr, sizeof(addr)), protocol, total);
    ret = ip_output_device(iface, buf, total, nexthop);
    memory_free(buf);
    return ret;
}

static uint16
ip_generate_id(void)
{
    static mutex_t mutex = MUTEX_INITIALIZER;
    static uint16 id = 128;
    uint16 ret;

    mutex_lock(&mutex);
    ret = id++;
    mutex_unlock(&mutex);
    return ret;
}

int
ip_output(uint8 protocol, const uint8 *data, uint len, ip_addr_t src, ip_addr_t dst)
{
    struct ip_route *route;
    struct ip_iface *iface;
    char addr[IP_ADDR_STR_LEN];
    ip_addr_t nexthop;
    uint16 id;

    if (src == IP_ADDR_ANY &&  dst == IP_ADDR_BROADCAST) {
        errorf("source address is required for broadcast addresses");
        return -1;
    }
    route = ip_route_lookup(dst);
    if (!route) {
        errorf("no route to host, dst=%s", ip_addr_ntop(dst, addr, sizeof(addr)));
        return -1;
    }
    iface = route->iface;
    if (src != IP_ADDR_ANY && src != iface->unicast) {
        errorf("unable to output with specified source address, src=%s", ip_addr_ntop(src, addr, sizeof(addr)));
        return -1;
    }
    nexthop = (route->nexthop != IP_ADDR_ANY) ? route->nexthop : dst;
    if (NET_IFACE(iface)->dev->mtu < IP_HDR_SIZE_MIN + len) {
        errorf("too long, dev=%s, mtu=%u < %u",
            NET_IFACE(iface)->dev->name, NET_IFACE(iface)->dev->mtu, IP_HDR_SIZE_MIN + len);
        return -1;
    }
    id = ip_generate_id();
    if (ip_output_core(iface, protocol, data, len, iface->unicast, dst, nexthop, id, 0) == -1) {
        errorf("ip_output_core() failure");
        return -1;
    }
    return len;
}

int
ip_init(void)
{
    if (net_protocol_register(NET_PROTOCOL_TYPE_IP, ip_input) == -1) {
        errorf("net_protocol_register() failure");
        return -1;
    }
    return 0;
}

/* ch9: 网络协议栈 - 以太网层实现 */
#include "platform.h"
#include "util.h"
#include "net.h"
#include "ether.h"

struct ether_hdr {
    uint8 dst[ETHER_ADDR_LEN];
    uint8 src[ETHER_ADDR_LEN];
    uint16 type;
};

const uint8 ETHER_ADDR_ANY[ETHER_ADDR_LEN] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
const uint8 ETHER_ADDR_BROADCAST[ETHER_ADDR_LEN] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};

/* 将一个字节转换为两位十六进制字符 */
static void
byte_to_hex(uint8 b, char *out)
{
    const char hex[] = "0123456789abcdef";
    out[0] = hex[(b >> 4) & 0x0f];
    out[1] = hex[b & 0x0f];
}

int
ether_addr_pton(const char *p, uint8 *n)
{
    int index;
    char *ep;
    long val;

    if (!p || !n) {
        return -1;
    }
    for (index = 0; index < ETHER_ADDR_LEN; index++) {
        val = strtol(p, &ep, 16);
        if (ep == p || val < 0 || val > 0xff || (index < ETHER_ADDR_LEN - 1 && *ep != ':')) {
            break;
        }
        n[index] = (uint8)val;
        p = ep + 1;
    }
    if (index != ETHER_ADDR_LEN || *ep != '\0') {
        return -1;
    }
    return 0;
}

char *
ether_addr_ntop(const uint8 *n, char *p, uint size)
{
    if (!n || !p || size < ETHER_ADDR_STR_LEN) {
        return 0;
    }
    /* 格式: xx:xx:xx:xx:xx:xx */
    byte_to_hex(n[0], p);
    p[2] = ':';
    byte_to_hex(n[1], p + 3);
    p[5] = ':';
    byte_to_hex(n[2], p + 6);
    p[8] = ':';
    byte_to_hex(n[3], p + 9);
    p[11] = ':';
    byte_to_hex(n[4], p + 12);
    p[14] = ':';
    byte_to_hex(n[5], p + 15);
    p[17] = '\0';
    return p;
}

int
ether_transmit_helper(struct net_device *dev, uint16 type, const uint8 *data, uint len, const void *dst, ether_transmit_func_t callback)
{
    uint8 *frame;
    struct ether_hdr *hdr;
    uint flen, pad = 0;
    int ret;

    frame = memory_alloc(ETHER_FRAME_SIZE_MAX);
    if (!frame) {
        return -1;
    }
    hdr = (struct ether_hdr *)frame;
    memmove(hdr->dst, dst, ETHER_ADDR_LEN);
    memmove(hdr->src, dev->addr, ETHER_ADDR_LEN);
    hdr->type = hton16(type);
    memmove(hdr+1, data, len);
    if (len < ETHER_PAYLOAD_SIZE_MIN) {
        pad = ETHER_PAYLOAD_SIZE_MIN - len;
    }
    flen = sizeof(*hdr) + len + pad;
    debugf("dev=%s, type=0x%04x, len=%u", dev->name, type, flen);
    ret = callback(dev, frame, flen) == (int)flen ? 0 : -1;
    memory_free(frame);
    return ret;
}

int
ether_input_helper(struct net_device *dev, ether_input_func_t callback)
{
    uint8 *frame;
    int flen;
    struct ether_hdr *hdr;
    uint16 type;
    int ret;

    frame = memory_alloc(ETHER_FRAME_SIZE_MAX);
    if (!frame) {
        return -1;
    }
    flen = callback(dev, frame, ETHER_FRAME_SIZE_MAX);
    if (flen < (int)sizeof(*hdr)) {
        errorf("too short");
        memory_free(frame);
        return -1;
    }
    hdr = (struct ether_hdr *)frame;
    if (memcmp(dev->addr, hdr->dst, ETHER_ADDR_LEN) != 0) {
        if (memcmp(ETHER_ADDR_BROADCAST, hdr->dst, ETHER_ADDR_LEN) != 0) {
            /* for other host */
            memory_free(frame);
            return -1;
        }
    }
    type = ntoh16(hdr->type);
    debugf("dev=%s, type=0x%04x, len=%d", dev->name, type, flen);
    ret = net_input_handler(type, (uint8 *)(hdr+1), flen - sizeof(*hdr), dev);
    memory_free(frame);
    return ret;
}

void
ether_setup_helper(struct net_device *dev)
{
    dev->type = NET_DEVICE_TYPE_ETHERNET;
    dev->mtu = ETHER_PAYLOAD_SIZE_MAX;
    dev->flags = (NET_DEVICE_FLAG_BROADCAST | NET_DEVICE_FLAG_NEED_ARP);
    dev->hlen = ETHER_HDR_SIZE;
    dev->alen = ETHER_ADDR_LEN;
    memmove(dev->broadcast, ETHER_ADDR_BROADCAST, ETHER_ADDR_LEN);
}

/* ch9: 网络协议栈 - ICMP层实现 */
#include "platform.h"
#include "util.h"
#include "ip.h"
#include "icmp.h"

#define ICMP_BUFSIZ IP_PAYLOAD_SIZE_MAX

struct icmp_hdr {
    uint8 type;
    uint8 code;
    uint16 sum;
    uint32 values;
};

struct icmp_echo {
    uint8 type;
    uint8 code;
    uint16 sum;
    uint16 id;
    uint16 seq;
};

static void
icmp_input(const uint8 *data, uint len, ip_addr_t src, ip_addr_t dst, struct ip_iface *iface)
{
    struct icmp_hdr *hdr;
    char addr1[IP_ADDR_STR_LEN];
    char addr2[IP_ADDR_STR_LEN];

    if (len < sizeof(*hdr)) {
        errorf("too short");
        return;
    }
    hdr = (struct icmp_hdr *)data;
    if (cksum16((uint16 *)data, len, 0) != 0) {
        errorf("checksum error");
        return;
    }
    debugf("%s => %s, len=%u", ip_addr_ntop(src, addr1, sizeof(addr1)), ip_addr_ntop(dst, addr2, sizeof(addr2)), len);
    switch (hdr->type) {
    case ICMP_TYPE_ECHO:
        /* Responds with the address of the received interface. */
        icmp_output(ICMP_TYPE_ECHOREPLY, hdr->code, hdr->values, (uint8 *)(hdr + 1), len - sizeof(*hdr), iface->unicast, src);
        break;
    default:
        /* ignore */
        break;
    }
}

int
icmp_output(uint8 type, uint8 code, uint32 values, const uint8 *data, uint len, ip_addr_t src, ip_addr_t dst)
{
    uint8 *buf;
    struct icmp_hdr *hdr;
    uint msg_len;
    char addr1[IP_ADDR_STR_LEN];
    char addr2[IP_ADDR_STR_LEN];
    int ret;

    buf = memory_alloc(ICMP_BUFSIZ);
    if (!buf) {
        errorf("memory_alloc() failure");
        return -1;
    }
    hdr = (struct icmp_hdr *)buf;
    hdr->type = type;
    hdr->code = code;
    hdr->sum = 0;
    hdr->values = values;
    memmove(hdr+1, data, len);
    msg_len = sizeof(*hdr) + len;
    hdr->sum = cksum16((uint16 *)hdr, msg_len, 0);
    debugf("%s => %s, len=%u", ip_addr_ntop(src, addr1, sizeof(addr1)), ip_addr_ntop(dst, addr2, sizeof(addr2)), msg_len);
    ret = ip_output(IP_PROTOCOL_ICMP, buf, msg_len, src, dst);
    memory_free(buf);
    return ret;
}

int
icmp_init(void)
{
    if (ip_protocol_register(IP_PROTOCOL_ICMP, icmp_input) == -1) {
        errorf("ip_protocol_register() failure");
        return -1;
    }
    return 0;
}

/* ch9: 网络协议栈 - 以太网层 */
#ifndef ETHER_H
#define ETHER_H

#include "platform.h"
#include "net.h"

#define ETHER_ADDR_LEN 6
#define ETHER_ADDR_STR_LEN 18 /* "xx:xx:xx:xx:xx:xx\0" */

#define ETHER_HDR_SIZE 14
#define ETHER_FRAME_SIZE_MIN   60 /* without FCS */
#define ETHER_FRAME_SIZE_MAX 1514 /* without FCS */
#define ETHER_PAYLOAD_SIZE_MIN (ETHER_FRAME_SIZE_MIN - ETHER_HDR_SIZE)
#define ETHER_PAYLOAD_SIZE_MAX (ETHER_FRAME_SIZE_MAX - ETHER_HDR_SIZE)

/* see https://www.iana.org/assignments/ieee-802-numbers/ieee-802-numbers.txt */
#define ETHER_TYPE_IP   0x0800
#define ETHER_TYPE_ARP  0x0806
#define ETHER_TYPE_IPV6 0x86dd

extern const uint8 ETHER_ADDR_ANY[ETHER_ADDR_LEN];
extern const uint8 ETHER_ADDR_BROADCAST[ETHER_ADDR_LEN];

extern int
ether_addr_pton(const char *p, uint8 *n);
extern char *
ether_addr_ntop(const uint8 *n, char *p, uint size);

typedef int (*ether_transmit_func_t)(struct net_device *dev, const uint8 *data, uint len);
typedef int (*ether_input_func_t)(struct net_device *dev, uint8 *buf, uint size);

extern int
ether_transmit_helper(struct net_device *dev, uint16 type, const uint8 *payload, uint plen, const void *dst, ether_transmit_func_t callback);
extern int
ether_input_helper(struct net_device *dev, ether_input_func_t callback);
extern void
ether_setup_helper(struct net_device *dev);

#endif

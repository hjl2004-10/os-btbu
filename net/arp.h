/* ch9: 网络协议栈 - ARP层 */
#ifndef ARP_H
#define ARP_H

#include "platform.h"
#include "net.h"
#include "ip.h"

#define ARP_RESOLVE_ERROR      -1
#define ARP_RESOLVE_INCOMPLETE  0
#define ARP_RESOLVE_FOUND       1

extern int
arp_resolve(struct net_iface *iface, ip_addr_t pa, uint8 *ha);

extern int
arp_init(void);

#endif

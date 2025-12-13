/* ch9: 网络协议栈 - UDP层 */
#ifndef UDP_H
#define UDP_H

#include "platform.h"
#include "ip.h"

extern int
udp_output(struct ip_endpoint *src, struct ip_endpoint *dst, const uint8 *buf, uint len);

extern int
udp_init(void);

extern int
udp_open(void);
extern int
udp_close(int id);
extern int
udp_bind(int index, struct ip_endpoint *local);
extern int
udp_sendto(int id, uint8 *buf, uint len, struct ip_endpoint *foreign);
extern int
udp_recvfrom(int id, uint8 *buf, uint size, struct ip_endpoint *foreign);

#endif

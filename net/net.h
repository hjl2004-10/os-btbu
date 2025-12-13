/* ch9: 网络协议栈 - 网络设备抽象层 */
#ifndef NET_H
#define NET_H

#include "platform.h"

#ifndef IFNAMSIZ
#define IFNAMSIZ 16
#endif

#define NET_DEVICE_TYPE_DUMMY     0x0000
#define NET_DEVICE_TYPE_LOOPBACK  0x0001
#define NET_DEVICE_TYPE_ETHERNET  0x0002

#define NET_DEVICE_FLAG_UP        0x0001
#define NET_DEVICE_FLAG_BROADCAST 0x0002
#define NET_DEVICE_FLAG_LOOPBACK  0x0008
#define NET_DEVICE_FLAG_P2P       0x0010
#define NET_DEVICE_FLAG_NEED_ARP  0x0080

#define NET_DEVICE_ADDR_LEN 16

#define NET_DEVICE_IS_UP(x) ((x)->flags & NET_DEVICE_FLAG_UP)
#define NET_DEVICE_STATE(x) (NET_DEVICE_IS_UP(x) ? "up" : "down")

/* NOTE: use same value as the Ethernet types */
#define NET_PROTOCOL_TYPE_IP   0x0800
#define NET_PROTOCOL_TYPE_ARP  0x0806
#define NTT_PROTOCOL_TYPE_IPV6 0x86dd

#define NET_IFACE_FAMILY_IP    1
#define NET_IFACE_FAMILY_IPV6  2

#define NET_IFACE(x) ((struct net_iface *)(x))

struct net_device {
    struct net_device *next;
    struct net_iface *ifaces; /* NOTE: if you want to add/delete the entries after net_run(), you need to protect ifaces with a mutex. */
    unsigned int index;
    char name[IFNAMSIZ];
    uint16 type;
    uint16 mtu;
    uint16 flags;
    uint16 hlen; /* header length */
    uint16 alen; /* address length */
    uint8 addr[NET_DEVICE_ADDR_LEN];
    union {
        uint8 peer[NET_DEVICE_ADDR_LEN];
        uint8 broadcast[NET_DEVICE_ADDR_LEN];
    };
    struct net_device_ops *ops;
    void *priv;
};

struct net_device_ops {
    int (*open)(struct net_device *dev);
    int (*close)(struct net_device *dev);
    int (*transmit)(struct net_device *dev, uint16 type, const uint8 *data, uint len, const void *dst);
};

struct net_iface {
    struct net_iface *next;
    struct net_device *dev; /* back pointer to parent */
    int family;
    /* depends on implementation of protocols. */
};

extern struct net_device *
net_device_alloc(void);
extern int
net_device_open(struct net_device *dev);
extern int
net_device_close(struct net_device *dev);
extern int
net_device_register(struct net_device *dev);
extern struct net_device *
net_device_by_index(unsigned int index);
extern struct net_device *
net_device_by_name(const char *name);
extern int
net_device_add_iface(struct net_device *dev, struct net_iface *iface);
extern struct net_iface *
net_device_get_iface(struct net_device *dev, int family);
extern int
net_device_output(struct net_device *dev, uint16 type, const uint8 *data, uint len, const void *dst);

extern int
net_protocol_register(uint16 type, void (*handler)(const uint8 *data, uint len, struct net_device *dev));

extern int
net_timer_register(struct timeval interval, void (*handler)(void));
extern int
net_timer_handler(void);

extern int
net_input_handler(uint16 type, const uint8 *data, uint len, struct net_device *dev);
extern int
net_softirq_handler(void);

extern int
net_event_subscribe(void (*handler)(void *arg), void *arg);
extern int
net_event_handler(void);
extern void
net_raise_event(void);

extern int
net_run(void);
extern void
net_shutdown(void);
extern int
net_init(void);

#endif

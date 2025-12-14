/* ch9: 网络协议栈 - VirtIO网络设备驱动 */
#include "platform.h"
#include "util.h"
#include "net.h"
#include "ether.h"
#include "ip.h"

/* VirtIO MMIO地址 - 网络设备使用VIRTIO1 (0x10002000) */
#define VIRTIO1 0x10002000L

#define R(r) ((volatile uint32 *)(VIRTIO1 + (r)))

/* VirtIO MMIO寄存器偏移 */
#define VIRTIO_MMIO_MAGIC_VALUE     0x000
#define VIRTIO_MMIO_VERSION         0x004
#define VIRTIO_MMIO_DEVICE_ID       0x008
#define VIRTIO_MMIO_VENDOR_ID       0x00c
#define VIRTIO_MMIO_DEVICE_FEATURES 0x010
#define VIRTIO_MMIO_DRIVER_FEATURES 0x020
#define VIRTIO_MMIO_GUEST_PAGE_SIZE 0x028  /* ch9: legacy模式需要 */
#define VIRTIO_MMIO_QUEUE_SEL       0x030
#define VIRTIO_MMIO_QUEUE_NUM_MAX   0x034
#define VIRTIO_MMIO_QUEUE_NUM       0x038
#define VIRTIO_MMIO_QUEUE_ALIGN     0x03c  /* ch9: legacy模式需要 */
#define VIRTIO_MMIO_QUEUE_PFN       0x040  /* ch9: legacy模式需要 */
#define VIRTIO_MMIO_QUEUE_READY     0x044
#define VIRTIO_MMIO_QUEUE_NOTIFY    0x050
#define VIRTIO_MMIO_INTERRUPT_STATUS 0x060
#define VIRTIO_MMIO_INTERRUPT_ACK   0x064
#define VIRTIO_MMIO_STATUS          0x070
#define VIRTIO_MMIO_QUEUE_DESC_LOW  0x080
#define VIRTIO_MMIO_QUEUE_DESC_HIGH 0x084
#define VIRTIO_MMIO_DRIVER_DESC_LOW 0x090
#define VIRTIO_MMIO_DRIVER_DESC_HIGH 0x094
#define VIRTIO_MMIO_DEVICE_DESC_LOW 0x0a0
#define VIRTIO_MMIO_DEVICE_DESC_HIGH 0x0a4

/* VirtIO状态位 */
#define VIRTIO_CONFIG_S_ACKNOWLEDGE 1
#define VIRTIO_CONFIG_S_DRIVER      2
#define VIRTIO_CONFIG_S_DRIVER_OK   4
#define VIRTIO_CONFIG_S_FEATURES_OK 8

/* VirtIO描述符标志 */
#define VRING_DESC_F_NEXT  1
#define VRING_DESC_F_WRITE 2

/* VirtIO特性位 */
#define VIRTIO_RING_F_EVENT_IDX     29
#define VIRTIO_NET_F_CSUM           0
#define VIRTIO_NET_F_GUEST_CSUM     1
#define VIRTIO_NET_F_MAC            5
#define VIRTIO_F_VERSION_1          32

#define VIRTIO_MMIO_CONFIG 0x100

/* 队列大小 */
#define NUM 8
#define QSIZE NUM
#define RX_BUF_SIZE 2048

/* 队列索引 */
#define RXQ 0
#define TXQ 1

/* VirtIO描述符 */
struct virtq_desc {
    uint64 addr;
    uint32 len;
    uint16 flags;
    uint16 next;
};

/* VirtIO可用环 */
struct virtq_avail {
    uint16 flags;
    uint16 idx;
    uint16 ring[NUM];
    uint16 unused;
};

/* VirtIO已用环元素 */
struct virtq_used_elem {
    uint32 id;
    uint32 len;
};

/* VirtIO已用环 */
struct virtq_used {
    uint16 flags;
    uint16 idx;
    struct virtq_used_elem ring[NUM];
};

/* VirtIO队列 - legacy模式使用连续内存 */
struct virtq {
    char pages[2 * PGSIZE] __attribute__((aligned(PGSIZE)));
    struct virtq_desc *desc;
    struct virtq_avail *avail;
    struct virtq_used *used;
    int num;
    int last_used_idx;
    char free[NUM];
};

/* VirtIO网络头 */
struct virtio_net_hdr {
#define VIRTIO_NET_HDR_F_NEEDS_CSUM 1
#define VIRTIO_NET_HDR_F_DATA_VALID 2
#define VIRTIO_NET_HDR_F_RSC_INFO 4
    uint8 flags;
#define VIRTIO_NET_HDR_GSO_NONE 0
#define VIRTIO_NET_HDR_GSO_TCPV4 1
#define VIRTIO_NET_HDR_GSO_UDP 3
#define VIRTIO_NET_HDR_GSO_TCPV6 4
#define VIRTIO_NET_HDR_GSO_ECN 0x80
    uint8 gso_type;
    uint16 hdr_len;
    uint16 gso_size;
    uint16 csum_start;
    uint16 csum_offset;
    uint16 num_buffers;
};

/* VirtIO网络设备结构 */
struct virtio_net {
    struct net_device *dev;
    uint32 status;
    uint64 features;
    mutex_t lock;
    struct virtq rx_q;
    struct virtq tx_q;
    char rx_bufs[QSIZE][RX_BUF_SIZE];
    char tx_bufs[QSIZE][RX_BUF_SIZE];
    char tx_status[QSIZE];
};

static struct virtio_net _nic0;

#define PRIV(x) ((struct virtio_net *)(x)->priv)

/* 初始化VirtIO队列 - legacy模式 */
static void
virtq_init(struct virtq *vq, int sel, int num)
{
    uint32 max;

    /* select queue */
    *R(VIRTIO_MMIO_QUEUE_SEL) = sel;

    /* check maximum queue size */
    max = *R(VIRTIO_MMIO_QUEUE_NUM_MAX);
    if (max == 0) {
        panic("virtio_net: queue not available");
    }
    if (max < (uint32)num) {
        panic("virtio_net: queue too short");
    }

    /* set queue size */
    vq->num = num;
    *R(VIRTIO_MMIO_QUEUE_NUM) = num;

    /* ch9: legacy模式 - 使用连续内存和QUEUE_PFN */
    memset(vq->pages, 0, sizeof(vq->pages));

    /* 设置队列物理页帧号 */
    *R(VIRTIO_MMIO_QUEUE_PFN) = ((uint64)vq->pages) >> PGSHIFT;

    /* legacy模式内存布局: desc在开始, avail紧随其后, used在第二页 */
    vq->desc = (struct virtq_desc *)vq->pages;
    vq->avail = (struct virtq_avail *)(vq->pages + NUM * sizeof(struct virtq_desc));
    vq->used = (struct virtq_used *)(vq->pages + PGSIZE);

    /* all descriptors start out unused */
    for (int i = 0; i < vq->num; i++) {
        vq->free[i] = 1;
    }
    vq->last_used_idx = 0;
}

/* 分配描述符 */
static int
virtq_alloc_desc(struct virtq *vq)
{
    for (int i = 0; i < vq->num; i++) {
        if (vq->free[i]) {
            vq->free[i] = 0;
            return i;
        }
    }
    return -1;
}

/* 释放描述符 */
static void
virtq_free_desc(struct virtq *vq, int i)
{
    if (i >= vq->num) {
        panic("virtio_net: virtq_free_desc: invalid index");
    }
    if (vq->free[i]) {
        panic("virtio_net: virtq_free_desc: freeing free descriptor");
    }
    vq->desc[i].addr = 0;
    vq->desc[i].len = 0;
    vq->desc[i].flags = 0;
    vq->desc[i].next = 0;
    vq->free[i] = 1;
}

/* 打开网络设备 */
static int
virtio_net_open(struct net_device *dev)
{
    struct virtio_net *nic = PRIV(dev);

    mutex_lock(&nic->lock);

    /* set receive buffers */
    for (int i = 0; i < QSIZE; i++) {
        nic->rx_q.desc[i].addr = (uint64)nic->rx_bufs[i];
        nic->rx_q.desc[i].len = RX_BUF_SIZE;
        nic->rx_q.desc[i].flags = VRING_DESC_F_WRITE;
        nic->rx_q.avail->ring[i] = i;
        nic->rx_q.free[i] = 0;
    }
    __sync_synchronize();
    nic->rx_q.avail->idx = QSIZE;
    nic->rx_q.last_used_idx = 0;

    /* tell device we're completely ready */
    nic->status |= VIRTIO_CONFIG_S_DRIVER_OK;
    *R(VIRTIO_MMIO_STATUS) = nic->status;

    /* notify the device of new RX buffers */
    *R(VIRTIO_MMIO_QUEUE_NOTIFY) = RXQ;

    mutex_unlock(&nic->lock);

    return 0;
}

/* 关闭网络设备 */
static int
virtio_net_close(struct net_device *dev)
{
    struct virtio_net *nic = PRIV(dev);

    mutex_lock(&nic->lock);

    nic->status = *R(VIRTIO_MMIO_STATUS);

    /* clear DRIVER_OK bit */
    nic->status &= ~VIRTIO_CONFIG_S_DRIVER_OK;
    *R(VIRTIO_MMIO_STATUS) = nic->status;

    mutex_unlock(&nic->lock);
    return 0;
}

/* 写入数据到网络设备 */
static int
virtio_net_write(struct net_device *dev, const uint8 *data, uint len)
{
    struct virtio_net *nic = PRIV(dev);
    int idx;
    void *buf;
    struct virtio_net_hdr *hdr;

    if (len > RX_BUF_SIZE) {
        return -1;
    }

    mutex_lock(&nic->lock);

    /* allocate descriptor */
    idx = virtq_alloc_desc(&nic->tx_q);
    if (idx == -1) {
        mutex_unlock(&nic->lock);
        return -1;
    }

    buf = nic->tx_bufs[idx];
    hdr = buf;

    /* setup virtio-net header */
    hdr->flags = 0;
    hdr->gso_type = VIRTIO_NET_HDR_GSO_NONE;
    hdr->hdr_len = 0;
    hdr->gso_size = 0;
    hdr->csum_start = 0;
    hdr->csum_offset = 0;
    hdr->num_buffers = 0;
    memmove(nic->tx_bufs[idx] + sizeof(*hdr), data, len);

    /* configure descriptor */
    nic->tx_q.desc[idx].addr = (uint64)nic->tx_bufs[idx];
    nic->tx_q.desc[idx].len = sizeof(*hdr) + len;
    nic->tx_q.desc[idx].flags = 0;

    /* deploy descriptor in the available ring */
    nic->tx_q.avail->ring[nic->tx_q.avail->idx % nic->tx_q.num] = idx;
    __sync_synchronize();
    nic->tx_q.avail->idx++;
    __sync_synchronize();

    mutex_unlock(&nic->lock);

    /* notify the device of a new TX packet */
    *R(VIRTIO_MMIO_QUEUE_NOTIFY) = TXQ;

    return len;
}

/* 传输数据包 */
static int
virtio_net_transmit(struct net_device *dev, uint16 type, const uint8 *packet, uint len, const void *dst)
{
    return ether_transmit_helper(dev, type, packet, len, dst, virtio_net_write);
}

/* 从网络设备读取数据 */
static int
virtio_net_read(struct net_device *dev, uint8 *buf, uint size)
{
    struct virtio_net *nic = PRIV(dev);

    int ring_idx = nic->rx_q.last_used_idx % nic->rx_q.num;
    int idx = nic->rx_q.used->ring[ring_idx].id;
    int len = nic->rx_q.used->ring[ring_idx].len;

    /* the actual data starts after the virtio-net header */
    int hdrlen = sizeof(struct virtio_net_hdr);
    memmove(buf, nic->rx_bufs[idx] + hdrlen, len - hdrlen);

    /* recycle the receive buffer descriptor */
    nic->rx_q.desc[idx].addr = (uint64)nic->rx_bufs[idx];
    nic->rx_q.desc[idx].len = RX_BUF_SIZE;

    /* return the descriptor to the available ring */
    nic->rx_q.avail->ring[nic->rx_q.avail->idx % nic->rx_q.num] = idx;

    return len - hdrlen;
}

/* 网络设备中断处理 */
void
virtio_net_intr(void)
{
    struct virtio_net *nic = &_nic0;

    mutex_lock(&nic->lock);

    /* acknowledge the interrupt and clear the status */
    *R(VIRTIO_MMIO_INTERRUPT_ACK) = *R(VIRTIO_MMIO_INTERRUPT_STATUS) & 0x3;
    __sync_synchronize();

    /* process completed descriptors from the tx used ring */
    while (nic->tx_q.last_used_idx != nic->tx_q.used->idx) {
        int ring_idx = nic->tx_q.last_used_idx % nic->tx_q.num;
        int idx = nic->tx_q.used->ring[ring_idx].id;
        virtq_free_desc(&nic->tx_q, idx);
        nic->tx_q.last_used_idx++;
    }

    /* process incoming packets from the rx used ring */
    while (nic->rx_q.last_used_idx != nic->rx_q.used->idx) {
        ether_input_helper(nic->dev, virtio_net_read);
        __sync_synchronize();
        nic->rx_q.avail->idx++;
        nic->rx_q.last_used_idx++;
    }
    __sync_synchronize();

    mutex_unlock(&nic->lock);

    /* notify the device of new RX buffers */
    *R(VIRTIO_MMIO_QUEUE_NOTIFY) = RXQ;

    intr_raise_irq(INTR_IRQ_SOFTIRQ);
}

/* 网络设备操作 */
struct net_device_ops virtio_net_ops = {
    .open = virtio_net_open,
    .close = virtio_net_close,
    .transmit = virtio_net_transmit,
};

/* 初始化VirtIO网络设备 */
void
virtio_net_init(void)
{
    struct virtio_net *nic = &_nic0;
    uint8 addr[6];
    struct net_device *dev;
    char mac[ETHER_ADDR_STR_LEN];

    mutex_init(&nic->lock);

    /* ch9: 调试输出，检测设备 */
    printf("virtio-net: probing at 0x%lx\n", (uint64)VIRTIO1);
    printf("virtio-net: magic=0x%x, version=%d, device_id=%d, vendor=0x%x\n",
          *R(VIRTIO_MMIO_MAGIC_VALUE),
          *R(VIRTIO_MMIO_VERSION),
          *R(VIRTIO_MMIO_DEVICE_ID),
          *R(VIRTIO_MMIO_VENDOR_ID));

    /* find virtio-net device */
    if (*R(VIRTIO_MMIO_MAGIC_VALUE) != 0x74726976 ||
        (*R(VIRTIO_MMIO_VERSION) != 1 && *R(VIRTIO_MMIO_VERSION) != 2) ||
        *R(VIRTIO_MMIO_DEVICE_ID) != 1 || /* network device */
        *R(VIRTIO_MMIO_VENDOR_ID) != 0x554d4551) {
        printf("virtio-net: device not found\n");
        return;
    }

    debugf("virtio-net: device found");

    /* reset device */
    nic->status = 0;
    *R(VIRTIO_MMIO_STATUS) = nic->status;

    /* set ACKNOWLEDGE status bit */
    nic->status |= VIRTIO_CONFIG_S_ACKNOWLEDGE;
    *R(VIRTIO_MMIO_STATUS) = nic->status;

    /* set DRIVER status bit */
    nic->status |= VIRTIO_CONFIG_S_DRIVER;
    *R(VIRTIO_MMIO_STATUS) = nic->status;

    /* negotiate features */
    nic->features = *R(VIRTIO_MMIO_DEVICE_FEATURES);
    nic->features &= ~(1ULL << VIRTIO_RING_F_EVENT_IDX);
    nic->features &= ~(1ULL << VIRTIO_NET_F_CSUM);
    *R(VIRTIO_MMIO_DRIVER_FEATURES) = nic->features;

    /* ch9: legacy模式 - 设置页大小，然后直接设置DRIVER_OK */
    *R(VIRTIO_MMIO_GUEST_PAGE_SIZE) = PGSIZE;

    /* tell device we're completely ready */
    nic->status |= VIRTIO_CONFIG_S_DRIVER_OK;
    *R(VIRTIO_MMIO_STATUS) = nic->status;

    /* read MAC address */
    if (nic->features & (1 << VIRTIO_NET_F_MAC)) {
        for (int i = 0; i < 6; i++) {
            addr[i] = *(volatile uint8 *)((uint64)(VIRTIO1 + VIRTIO_MMIO_CONFIG + i));
        }
    } else {
        printf("virtio-net: device does not provide a MAC address, using default\n");
        /* use default MAC */
        addr[0] = 0x52;
        addr[1] = 0x54;
        addr[2] = 0x00;
        addr[3] = 0x12;
        addr[4] = 0x34;
        addr[5] = 0x56;
    }

    /* initialize TXQ */
    virtq_init(&nic->tx_q, TXQ, QSIZE);

    /* initialize RXQ */
    virtq_init(&nic->rx_q, RXQ, QSIZE);
    for (int i = 0; i < QSIZE; i++) {
        nic->rx_q.desc[i].addr = (uint64)nic->rx_bufs[i];
        nic->rx_q.desc[i].len = RX_BUF_SIZE;
        nic->rx_q.desc[i].flags = VRING_DESC_F_WRITE;
        nic->rx_q.avail->ring[i] = i;
        nic->rx_q.free[i] = 0;
    }
    __sync_synchronize();
    nic->rx_q.avail->idx = QSIZE;

    /* setup device driver structure */
    dev = net_device_alloc();
    if (!dev) {
        errorf("virtio-net: net_device_alloc() failure");
        return;
    }
    ether_setup_helper(dev);
    memmove(dev->addr, addr, sizeof(addr));
    dev->priv = nic;
    dev->ops = &virtio_net_ops;
    if (net_device_register(dev) == -1) {
        errorf("virtio-net: net_device_register() failure");
        memory_free(dev);
        return;
    }
    nic->dev = dev;

    printf("virtio-net: initialized, addr=%s\n", ether_addr_ntop(dev->addr, mac, sizeof(mac)));

    /* ch9: 配置IP地址和默认网关 (QEMU用户网络模式) */
    {
        struct ip_iface *iface;
        /* QEMU用户网络: 虚拟机IP 10.0.2.15, 网关 10.0.2.2 */
        iface = ip_iface_alloc("10.0.2.15", "255.255.255.0");
        if (!iface) {
            printf("virtio-net: ip_iface_alloc() failure\n");
            return;
        }
        if (ip_iface_register(dev, iface) == -1) {
            printf("virtio-net: ip_iface_register() failure\n");
            return;
        }
        if (ip_route_set_default_gateway(iface, "10.0.2.2") == -1) {
            printf("virtio-net: ip_route_set_default_gateway() failure\n");
            return;
        }
        printf("virtio-net: IP configured, addr=10.0.2.15, gateway=10.0.2.2\n");
    }
}

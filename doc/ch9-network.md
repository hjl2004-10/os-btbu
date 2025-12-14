# Ch9: 网络协议栈实现

## 概述

本章实现了一个完整的TCP/IP网络协议栈，基于xv6-riscv-net项目的microps协议栈移植适配到uCore。支持以太网、ARP、IP、ICMP、UDP、TCP协议，以及VirtIO网络设备驱动。

## 文件结构

```
os-btbu/net/
├── platform.h      # uCore平台适配层头文件
├── platform.c      # 平台适配实现（时间、随机数、字符串函数）
├── util.h          # 工具函数头文件
├── util.c          # 工具函数（队列、字节序转换、校验和）
├── net.h           # 网络设备抽象层头文件
├── net.c           # 网络核心层实现
├── ether.h         # 以太网层头文件
├── ether.c         # 以太网帧处理
├── ip.h            # IP层头文件
├── ip.c            # IP协议实现（路由表、分片）
├── arp.h           # ARP层头文件
├── arp.c           # 地址解析协议实现
├── icmp.h          # ICMP层头文件
├── icmp.c          # ICMP协议实现（ping响应）
├── udp.h           # UDP层头文件
├── udp.c           # UDP协议实现
├── tcp.h           # TCP层头文件
├── tcp.c           # TCP协议实现（完整状态机）
└── virtio_net.c    # VirtIO网络设备驱动
```

## 协议栈架构

```
+------------------+
|   应用层 (HTTP)  |
+------------------+
|  TCP / UDP 层    |
+------------------+
|    ICMP 层       |
+------------------+
|     IP 层        |
+------------------+
|    ARP 层        |
+------------------+
|   以太网层       |
+------------------+
| VirtIO网卡驱动   |
+------------------+
|   QEMU虚拟网卡   |
+------------------+
```

## 主要数据结构

### 网络设备 (net_device)
```c
struct net_device {
    struct net_device *next;
    struct net_iface *ifaces;    /* 网络接口链表 */
    unsigned int index;
    char name[IFNAMSIZ];         /* 设备名如 "net0" */
    uint16 type;                 /* 设备类型 */
    uint16 mtu;                  /* 最大传输单元 */
    uint16 flags;                /* 设备标志 */
    uint8 addr[NET_DEVICE_ADDR_LEN];  /* MAC地址 */
    struct net_device_ops *ops;  /* 设备操作函数 */
    void *priv;                  /* 私有数据 */
};
```

### IP接口 (ip_iface)
```c
struct ip_iface {
    struct net_iface iface;
    struct ip_iface *next;
    ip_addr_t unicast;    /* IP地址 */
    ip_addr_t netmask;    /* 子网掩码 */
    ip_addr_t broadcast;  /* 广播地址 */
};
```

### TCP控制块 (tcp_pcb)
```c
struct tcp_pcb {
    int state;              /* TCP状态 */
    struct ip_endpoint local;
    struct ip_endpoint foreign;
    struct {
        uint32 nxt, una;    /* 发送序号 */
        uint16 wnd;         /* 发送窗口 */
    } snd;
    struct {
        uint32 nxt;         /* 接收序号 */
        uint16 wnd;         /* 接收窗口 */
    } rcv;
    uint8 buf[65535];       /* 接收缓冲区 */
    struct sched_ctx ctx;   /* 调度上下文 */
    struct queue_head queue;/* 重传队列 */
};
```

## API接口

### 网络初始化
```c
int net_init(void);           /* 初始化协议栈 */
int net_run(void);            /* 启动网络 */
void virtio_net_init(void);   /* 初始化VirtIO网卡 */
```

### IP配置
```c
struct ip_iface *ip_iface_alloc(const char *addr, const char *netmask);
int ip_iface_register(struct net_device *dev, struct ip_iface *iface);
int ip_route_set_default_gateway(struct ip_iface *iface, const char *gateway);
```

### UDP接口
```c
int udp_open(void);
int udp_bind(int id, struct ip_endpoint *local);
int udp_sendto(int id, uint8 *buf, uint len, struct ip_endpoint *foreign);
int udp_recvfrom(int id, uint8 *buf, uint size, struct ip_endpoint *foreign);
int udp_close(int id);
```

### TCP接口
```c
int tcp_open(void);
int tcp_bind(int id, struct ip_endpoint *local);
int tcp_listen(int id, int backlog);
int tcp_accept(int id, struct ip_endpoint *foreign);
int tcp_connect(int id, struct ip_endpoint *foreign);
int tcp_send(int id, uint8 *data, uint len);
int tcp_receive(int id, uint8 *buf, uint size);
int tcp_close(int id);
```

## 平台适配

### 内存管理
```c
void *memory_alloc(uint size);  /* 使用kalloc分配页面 */
void memory_free(void *ptr);    /* 使用kfree释放 */
```

### 互斥锁
```c
typedef struct spinlock mutex_t;
mutex_lock(&mutex);    /* 使用acquire */
mutex_unlock(&mutex);  /* 使用release */
```

### 调度
```c
sched_sleep(&ctx, &mutex, NULL);  /* 使用sleep */
sched_wakeup(&ctx);               /* 使用wakeup */
```

## VirtIO网络驱动

### MMIO地址
- VIRTIO1: 0x10002000 (网络设备)
- VIRTIO0: 0x10001000 (块设备，已被使用)

### 队列结构
- RXQ (队列0): 接收队列
- TXQ (队列1): 发送队列
- 每个队列8个描述符

### 数据收发流程

**发送流程:**
1. 分配TX描述符
2. 填充virtio-net头部
3. 复制数据到发送缓冲区
4. 添加到可用环
5. 通知设备 (QUEUE_NOTIFY)

**接收流程:**
1. 中断触发
2. 从已用环获取描述符
3. 解析virtio-net头部
4. 将数据传递给以太网层
5. 回收描述符到可用环

## QEMU网络配置

### 启动参数
```bash
qemu-system-riscv64 \
  -machine virt \
  -device virtio-net-device,netdev=net0 \
  -netdev user,id=net0,hostfwd=tcp::8080-:80
```

### 网络拓扑 (user模式)
```
+----------------+          +----------------+
|   Guest OS     |          |   Host OS      |
|  10.0.2.15     |  NAT     |                |
|                | <------> |  QEMU SLIRP    |
|  Gateway:      |          |  10.0.2.2      |
|  10.0.2.2      |          |                |
+----------------+          +----------------+
```

### 默认配置
- Guest IP: 10.0.2.15
- 网关: 10.0.2.2
- DNS: 10.0.2.3
- 子网掩码: 255.255.255.0

## 集成步骤

### 1. 修改Makefile
```makefile
# 添加net目录的源文件
OBJS += \
  net/platform.o \
  net/util.o \
  net/net.o \
  net/ether.o \
  net/ip.o \
  net/arp.o \
  net/icmp.o \
  net/udp.o \
  net/tcp.o \
  net/virtio_net.o
```

### 2. 内核初始化
```c
void main() {
    // ... 其他初始化 ...

    // 初始化网络协议栈
    net_init();
    virtio_net_init();

    // 配置网络接口
    struct net_device *dev = net_device_by_name("net0");
    if (dev) {
        struct ip_iface *iface = ip_iface_alloc("10.0.2.15", "255.255.255.0");
        ip_iface_register(dev, iface);
        ip_route_set_default_gateway(iface, "10.0.2.2");
    }

    // 启动网络
    net_run();

    // ... 继续其他初始化 ...
}
```

### 3. 中断处理
```c
void devintr() {
    int irq = plic_claim();
    if (irq == VIRTIO1_IRQ) {
        virtio_net_intr();
    }
    // ...
}
```

## 测试方法

### Ping测试
从主机ping虚拟机:
```bash
ping 10.0.2.15
```

### TCP连接测试
```c
// 在内核中创建TCP连接
int sock = tcp_open();
struct ip_endpoint server = {
    .addr = ip_addr_pton("93.184.216.34"),  // example.com
    .port = hton16(80)
};
tcp_connect(sock, &server);

// 发送HTTP请求
char *req = "GET / HTTP/1.0\r\nHost: example.com\r\n\r\n";
tcp_send(sock, (uint8*)req, strlen(req));

// 接收响应
char buf[4096];
int n = tcp_receive(sock, (uint8*)buf, sizeof(buf));

tcp_close(sock);
```

## 限制和注意事项

1. **不支持IP分片重组**: 大于MTU的包会被丢弃
2. **TCP窗口固定**: 接收窗口为65535字节
3. **单网卡**: 目前只支持一个网络设备
4. **无DHCP**: 需要手动配置IP地址
5. **无DNS解析**: 需要使用IP地址直接连接

## 调试记录

> 这是一次在未知领域的全新探索，从零开始在教学操作系统中实现网络协议栈并成功调用外部AI API。以下记录了整个调试过程中遇到的四个关键问题及其解决方案。

### Bug 1: IP路由失败 - 网络设备未配置IP地址

**现象**：
```
ch9: Testing AI API...
[ERROR 0--1]ip_route_get_iface() failure
ch9: ai_chat failed, error_code=-1
```

**分析**：
TCP连接建立时，`ip_route_get_iface()` 无法找到合适的网络接口。这是因为 `virtio_net_init()` 只初始化了设备驱动，但没有配置IP地址和路由。

**解决方案**：
在 `net/virtio_net.c` 的 `virtio_net_init()` 中添加IP配置：
```c
/* ch9: 配置IP地址 (QEMU user网络使用10.0.2.x网段) */
iface = ip_iface_alloc("10.0.2.15", "255.255.255.0");
ip_iface_register(dev, iface);
ip_route_set_default_gateway(iface, "10.0.2.2");
```

QEMU user模式网络（NAT）使用固定的10.0.2.x网段，其中10.0.2.2是默认网关。

---

### Bug 2: IP路由仍然失败 - 字节序问题

**现象**：
添加IP配置后，仍然报告路由失败。

**分析**：
`net/http.c` 中的 `parse_ip()` 函数返回主机字节序，然后代码又调用了 `hton32()` 进行转换：
```c
foreign.addr = hton32(parse_ip(req->host));  /* 错误！双重转换 */
```

但实际上 `ip_addr_pton()` 函数存储的就是网络字节序（按字节顺序存储），所以 `parse_ip()` 应该返回网络字节序。

**解决方案**：
重写 `parse_ip()` 直接按字节存储（网络字节序），移除多余的 `hton32()` 调用：
```c
static ip_addr_t parse_ip(const char *s) {
    ip_addr_t addr;
    uint8 *p = (uint8 *)&addr;  /* 按字节存储 = 网络字节序 */
    // ... 解析每个字节
    return addr;
}

foreign.addr = parse_ip(req->host);  /* 直接使用，不需要转换 */
```

**经验**：网络编程中字节序是常见陷阱。IP地址有两种存储方式：
- **网络字节序**（大端）：按字节顺序存储，如 `192.168.1.1` 存为 `{0xC0, 0xA8, 0x01, 0x01}`
- **主机字节序**：取决于CPU架构，RISC-V是小端

---

### Bug 3: VirtIO设备未找到 - Legacy模式与Modern模式

**现象**：
使用 `printf` 调试输出设备信息后发现：
```
virtio-net: magic=0x74726976, version=1, device_id=1, vendor=0x554d4551
virtio-net: device not found
```

**分析**：
QEMU的virtio-net设备使用**Legacy模式**（version=1），而代码检查的是**Modern模式**（version=2）：
```c
if (*R(VIRTIO_MMIO_VERSION) != 2) {  /* 只接受modern模式 */
    return -1;
}
```

Legacy模式和Modern模式在队列初始化上有重大区别：

| 特性 | Legacy模式 (v1) | Modern模式 (v2) |
|------|----------------|-----------------|
| 队列内存 | 连续两页对齐内存 | 分散的desc/avail/used |
| 队列地址 | QUEUE_PFN (页帧号) | QUEUE_DESC_LOW/HIGH等 |
| 页大小 | 需要设置GUEST_PAGE_SIZE | 不需要 |
| FEATURES_OK | 不需要 | 必须设置 |

**解决方案**：
实现Legacy模式队列初始化：
```c
/* Legacy模式：使用连续内存和QUEUE_PFN寄存器 */
struct virtq {
    char pages[2 * PGSIZE] __attribute__((aligned(PGSIZE)));
    struct virtq_desc *desc;
    struct virtq_avail *avail;
    struct virtq_used *used;
    // ...
};

static void virtq_init(struct virtq *vq, int sel, int num) {
    *R(VIRTIO_MMIO_QUEUE_SEL) = sel;
    *R(VIRTIO_MMIO_QUEUE_NUM) = num;
    memset(vq->pages, 0, sizeof(vq->pages));
    *R(VIRTIO_MMIO_QUEUE_PFN) = ((uint64)vq->pages) >> PGSHIFT;

    /* 队列布局：desc在页首，avail紧随其后，used在第二页 */
    vq->desc = (struct virtq_desc *)vq->pages;
    vq->avail = (struct virtq_avail *)(vq->pages + NUM * sizeof(struct virtq_desc));
    vq->used = (struct virtq_used *)(vq->pages + PGSIZE);
}
```

这个布局与 `os/virtio_disk.c` 中的磁盘驱动完全一致，因为它们都使用Legacy模式。

---

### Bug 4: 程序挂起 - 中断处理与忙等待

**现象**：
```
virtio-net: initialized
virtio-net: IP configured
ch9: Testing AI API...
（程序卡住，没有任何输出）
```

**分析**：
`sched_sleep()` 函数用于等待网络数据到达。原实现使用关中断的忙等待：
```c
static inline int sched_sleep(...) {
    intr_off();  /* 关中断 */
    while (!ctx->ready) {
        /* 忙等待，但中断被禁用了！ */
    }
}
```

问题在于：网络数据到达时会触发中断，中断处理程序设置 `ctx->ready = 1`。但如果中断被禁用，中断处理程序永远不会执行，`ctx->ready` 永远不会变为1，形成死锁。

**解决方案**：
在等待循环中开启中断，并主动轮询网络事件：
```c
extern int net_softirq_handler(void);
extern int net_timer_handler(void);

static inline int sched_sleep(...) {
    int timeout_loops = 100000000;
    mutex_unlock(mutex);

    while (!ctx->ready && !ctx->interrupted && timeout_loops > 0) {
        intr_on();  /* 开中断，让网络中断能够到达 */

        /* 主动轮询网络软中断 */
        if (net_pending & INTR_IRQ_SOFTIRQ) {
            mutex_lock(&net_pendinglock);
            net_pending &= ~INTR_IRQ_SOFTIRQ;
            mutex_unlock(&net_pendinglock);
            net_softirq_handler();
        }

        /* 处理定时器（TCP超时重传等） */
        net_timer_handler();

        __sync_synchronize();
        timeout_loops--;
    }

    intr_off();
    mutex_lock(mutex);
    // ...
}
```

**经验**：在没有完整进程调度的内核中实现阻塞I/O，需要在等待时：
1. 开启中断让设备中断能够到达
2. 主动轮询处理挂起的事件
3. 设置超时防止无限等待

---

### 最终成果

经过四个关键bug的修复，成功实现了从uCore内核直接调用外部AI API：

```
virtio-net: initialized successfully
virtio-net: IP configured: 10.0.2.15/24, gateway: 10.0.2.2
ch9: Testing AI API...
ch9: AI response: Hello! How can I assist you today? 😊
```

这标志着我们在教学操作系统中成功实现了完整的网络协议栈（Ethernet/ARP/IP/ICMP/UDP/TCP），并通过HTTP客户端与真实的云端AI服务进行了通信。

### 调试技巧总结

1. **日志级别**：`infof()` 等宏可能被编译时日志级别过滤，调试时可直接使用 `printf()`
2. **读取硬件寄存器**：直接打印VirtIO寄存器值，帮助理解设备实际状态
3. **参考已有代码**：`virtio_disk.c` 的实现为网络驱动提供了宝贵参考
4. **分层调试**：从底向上逐层验证：设备驱动 → IP配置 → TCP连接 → HTTP请求

## 参考资料

- [xv6-riscv-net](https://github.com/pandax381/xv6-net) - 原始网络协议栈实现
- [microps](https://github.com/pandax381/microps) - 教学用TCP/IP协议栈
- [VirtIO规范](https://docs.oasis-open.org/virtio/virtio/v1.1/virtio-v1.1.pdf)
- [RFC 793](https://tools.ietf.org/html/rfc793) - TCP协议规范

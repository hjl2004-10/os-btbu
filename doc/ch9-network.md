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

## 参考资料

- [xv6-riscv-net](https://github.com/pandax381/xv6-net) - 原始网络协议栈实现
- [microps](https://github.com/pandax381/microps) - 教学用TCP/IP协议栈
- [VirtIO规范](https://docs.oasis-open.org/virtio/virtio/v1.1/virtio-v1.1.pdf)
- [RFC 793](https://tools.ietf.org/html/rfc793) - TCP协议规范

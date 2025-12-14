# os-btbu
操作系统
队伍名称：灵汐队
学校：北京工商大学
开发人员：何俊林，许智辰
文档：狄昊然
12月  2025

## 更新日志

### 2025-11-25
- 导入清华ucore（rcore的c语言版）ch8内容作为基底，并打算补充前面章节的程序补充（ch1和ch2为环境搭建故无内容添加）
### 2025-11-30
- 实现 ch3 sys_trace 系统调用（读内存、写内存、统计syscall调用次数）
- 实现 ch4 mmap/munmap 系统调用（匿名内存映射）
- 修复 sys_trace 虚存权限检查（读操作检查PTE_R，写操作检查PTE_W）
- 修复死锁检测算法的竞态条件问题

### 2025-12-13
- 实现 ch5 sys_spawn 系统调用（创建新进程并执行程序，相当于fork+exec但不复制内存）
- 实现 ch5 sys_set_priority 系统调用（设置进程优先级）
- 实现 stride 调度算法（基于优先级的进程调度，优先级越高获得CPU时间越多）
- 通过 ch5b_usertest 和 ch5t_usertest 全部测试
- 实现 ch6 sys_linkat 系统调用（创建硬链接）
- 实现 ch6 sys_unlinkat 系统调用（删除硬链接）
- 实现 ch6 sys_fstat 系统调用（获取文件状态信息）
- 为 inode 添加 nlink 字段支持硬链接计数
- 修复 freewalk 函数以支持 mmap 区域的正确清理
- 通过 ch6b_usertest 和 ch6_usertest 全部测试
- 添加 ch3-ch6 教学文档（doc/目录）
- 添加 ch9 TCP/IP 网络协议栈（移植自 xv6-riscv-net/microps）
  - 支持以太网、ARP、IP、ICMP、UDP、TCP 协议
  - VirtIO 网络设备驱动（QEMU virtio-net-device）
  - 完整的 TCP 状态机实现
  - 为后续实现 HTTP 客户端调用 AI API 做准备

### 2025-12-14
- 集成网络协议栈到内核
  - 修改 Makefile 支持 net/ 目录编译
  - 添加 VIRTIO1 网络设备内存映射和中断处理
  - 在 main.c 中初始化网络协议栈
  - QEMU 启动参数添加 virtio-net-device
- 实现 HTTP 客户端（net/http.c）
  - 支持 HTTP GET/POST 请求
  - URL 解析、请求构建、响应解析
  - 支持 Authorization 头（用于API认证）
- 实现 AI API 客户端（net/ai_api.c）
  - 支持 OpenAI 兼容的 chat/completions 接口
  - JSON 请求构建和响应解析
  - 可配置 host、port、api_key、model
- **成功从内核调用外部 AI API**
  - 测试服务器：119.3.217.132:8000
  - 模型：lingxi-1
  - 收到响应："Hello! How can I assist you today? 😊"

## ch9 网络协议栈调试记录

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


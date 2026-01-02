# lab6

## 第六章：文件系统实现 - 核心数据结构

## 概述

本章介绍 uCore 操作系统中文件系统的核心数据结构。文件系统是操作系统中负责持久化存储和管理文件的关键组件。我们将深入分析超级块、inode、目录项和缓存块的设计与实现。

---

## 1. 超级块 (Superblock)

### 结构定义

```c
struct superblock {
    uint magic;     // 必须等于 FSMAGIC
    uint size;      // 文件系统镜像的大小(以块为单位)
    uint nblocks;   // 数据块的数量
    uint ninodes;   // inode 的数量
    uint inodestart;// 第一个 inode 块的块号
    uint bmapstart; // 第一个空闲位图块的块号
};
```

### 详细说明

**作用**：超级块是文件系统的"元数据管理中心"，固定存储在文件系统的特定位置，用来描述整个文件系统的布局和状态。

**字段详解**：

- **`magic`**：魔数，用于文件系统合法性校验
  - 必须等于 `FSMAGIC`，这是一个预定义的常量
  - 作用：防止加载错误的文件系统类型或损坏的文件系统
  - 类似于文件的"签名"或"指纹"

- **`size`**：文件系统镜像的总大小（以块为单位）
  - 表示整个文件系统占用的磁盘块总数
  - 用于边界检查，防止访问超出文件系统范围的块

- **`nblocks`**：数据块的数量
  - 记录文件系统中可用于存储文件内容的数据块总数
  - 不包括元数据块（如超级块、inode 位图、数据块位图等）

- **`ninodes`**：inode 的总数
  - 表示文件系统可以支持的文件数量上限
  - 每个文件对应一个 inode，因此这决定了文件系统的文件容量

- **`inodestart`**：第一个 inode 块的块号
  - 指示 inode 区域在磁盘上的起始位置
  - 通过这个偏移量，系统可以快速定位并读取任意 inode

- **`bmapstart`**：第一个空闲位图块的块号
  - 指向数据块位图（free map）的起始位置
  - 位图用于记录哪些数据块是空闲的，哪些已被分配

**为什么超级块重要？**
- 它是文件系统的"地图"，没有它就无法理解磁盘上的数据组织
- 它的固定位置使得系统启动时能快速找到文件系统
- 它的元数据帮助系统进行空间分配和回收

---

## 2. 磁盘 Inode (dinode)

### 结构定义

```c
struct dinode {
    short type;             // 文件类型
    short pad[3];
    uint size;              // 文件大小(字节)
    uint addrs[NDIRECT + 1];// 数据块地址
};
```

### 详细说明

**作用**：磁盘 inode 是文件元数据在磁盘上的持久化存储形式。每个文件对应一个 dinode，记录了文件类型、大小和数据块位置。

**字段详解**：

- **`type`**：文件类型
  - 标识文件是普通文件、目录还是特殊设备文件
  - 常见类型：普通文件、目录文件、设备文件等
  - 系统通过这个字段决定如何处理文件内容

- **`pad[3]`**：填充字段（3 个 short）
  - **重要**：这个字段用于结构体大小对齐，不要随意修改！
  - 原因：磁盘布局是固定的，改变结构体大小会导致文件系统不兼容
  - 作用：确保 dinode 大小是合适的值（如 64 字节），便于磁盘块管理

- **`size`**：文件大小（字节）
  - 记录文件的实际字节数
  - 注意：文件大小可能不是块大小的整数倍，最后一个数据块可能未完全使用

- **`addrs[NDIRECT + 1]`**：数据块地址数组
  - `NDIRECT`：直接数据块的数量（通常是 12 个）
  - `addrs[0]` 到 `addrs[NDIRECT-1]`：直接指向数据块
  - `addrs[NDIRECT]`：间接块（指向一个包含更多数据块地址的块）
  - 这种设计支持小文件的高效访问和大文件的扩展

**直接块与间接块的区别**：
- **直接块**：直接存储文件数据的块号，访问速度快
- **间接块**：存储一个指针，指向一个包含更多数据块地址的块，用于支持大文件
- 例如：NDIRECT=12 时，小文件（≤12×块大小）只需直接块，大文件需要间接块

**为什么不要随意修改 `pad` 字段？**
- 磁盘格式一旦确定，所有文件系统镜像都依赖这个格式
- 修改 `pad` 会改变 dinode 大小，导致旧文件系统无法读取
- 如需扩展，应使用新增字段并保持向后兼容

---

## 3. 内存 Inode (inode)

### 结构定义

```c
struct inode {
    uint dev;           // 设备号
    uint inum;          // Inode 编号
    int ref;            // 引用计数
    int valid;          // inode 是否已从磁盘读取?
    short type;         // 磁盘 inode 的副本
    uint size;
    uint addrs[NDIRECT+1];  // 数据块号
};
```

### 详细说明

**作用**：内存 inode 是磁盘 inode 的缓存副本，用于提高文件访问性能。它在内存中维护磁盘 inode 的副本，并添加了管理字段。

**字段详解**：

- **`dev`**：设备号
  - 标识这个 inode 所属的块设备
  - 支持多文件系统环境，区分不同的磁盘分区

- **`inum`**：inode 编号
  - 在文件系统内唯一标识一个 inode
  - 系统通过 `dev + inum` 组合来定位具体的文件

- **`ref`**：引用计数
  - 记录有多少个地方正在使用这个 inode
  - 作用：防止 inode 被过早释放
  - 当 `ref` 降为 0 时，inode 可以被回收

- **`valid`**：有效标志
  - 标识这个内存 inode 是否已从磁盘读取
  - `0`：内存中的数据无效，需要从磁盘加载
  - `1`：内存中的数据有效，是磁盘 inode 的最新副本
  - 延迟读取优化：只有真正需要时才读取磁盘

- **`type`**：文件类型（从 dinode 复制）
  - 磁盘 inode 的 `type` 字段的副本
  - 用于快速判断文件类型，无需访问磁盘

- **`size`**：文件大小（从 dinode 复制）
  - 磁盘 inode 的 `size` 字段的副本
  - 用于文件读写操作的边界检查

- **`addrs[NDIRECT+1]`**：数据块地址数组（从 dinode 复制）
  - 磁盘 inode 的 `addrs` 字段的副本
  - 在内存中访问文件数据时使用这些地址

**内存 Inode vs 磁盘 Inode**：

| 特性 | 磁盘 Inode (dinode) | 内存 Inode (inode) |
|------|:--------------------|:------------------|
| 存储位置 | 磁盘 | 内存 |
| 大小限制 | 固定，不能随便改 | 可以随意添加字段 |
| 额外字段 | 无 | dev, inum, ref, valid |
| 作用 | 持久化存储 | 读写缓存，性能优化 |

**引用计数的重要性**：
- 多个进程可能同时打开同一个文件，共享同一个 inode
- `ref` 计数确保 inode 不会在某个进程还在使用时被释放
- 实现"写时复制"（Copy-on-Write）等高级特性

---

## 4. 目录项 (Directory Entry)

### 结构定义

```c
struct dirent {
    ushort inum;
    char name[DIRSIS];
};
```

### 详细说明

**作用**：目录项实现了文件名到 inode 编号的映射。目录本质上是一个文件，其数据块由 `dirent` 数组组成，每个目录项记录一个文件的信息。

**字段详解**：

- **`inum`**：inode 编号（无符号短整型）
  - 指向文件的 inode
  - 通过这个编号，系统可以找到文件的完整元数据
  - 值为 0 表示空闲的目录项

- **`name[DIRSIZ]`**：文件名字符串
  - `DIRSIS` 应为 `DIRSIZ`，表示文件名的最大长度
  - 通常定义为 14 或 16 字节（不包括终止符）
  - 文件名以空字符 `'\0'` 结尾
  - 如果文件名长度不足 `DIRSIS`，剩余空间填充 0

**目录的查找过程**：
1. 读取目录文件的数据块
2. 遍历数据块中的 `dirent` 数组
3. 逐个比较 `dirent.name` 与目标文件名
4. 如果匹配，返回对应的 `dirent.inum`
5. 使用 `inum` 加载文件 inode，获取文件元数据

**为什么目录项设计简单？**
- 简化实现：使用数组 + 线性搜索，代码简单
- 小目录高效：对于文件数量少的目录，线性搜索足够快
- 教学友好：便于理解文件系统的基本原理

**可能的优化**（未实现）：
- 哈希表：O(1) 查找时间
- B+ 树：适合包含大量文件的目录
- 排序数组：支持二分查找

---

## 5. 缓冲区 (Buffer Cache)

### 结构定义

```c
struct buf {
    int valid;   // 数据是否已从磁盘读取?
    int disk;    // 磁盘是否"拥有"这个缓冲区?
    uint dev;
    uint blockno;
    uint refcnt;
    struct buf *prev; // LRU 缓存链表
    struct buf *next;
    uchar data[BSIZE];
};
```

### 详细说明

**作用**：缓冲区是磁盘块在内存中的缓存，用于减少磁盘 I/O 操作，提高文件系统性能。缓冲区采用 LRU（Least Recently Used）策略管理。

**字段详解**：

- **`valid`**：有效标志
  - `0`：缓冲区中的数据无效或未初始化
  - `1`：缓冲区中的数据有效，是磁盘块的最新副本
  - 在读取磁盘块前，先检查 `valid`，如果有效则直接使用

- **`disk`**：磁盘拥有标志
  - 标识磁盘是否"拥有"这个缓冲区
  - 用于延迟写（write-back）策略
  - 如果缓冲区被修改但 `disk` 仍为真，表示数据尚未写回磁盘

- **`dev`**：设备号
  - 标识这个缓冲区对应的块设备
  - 支持多设备环境，不同设备的缓冲区分开管理

- **`blockno`**：块号
  - 标识这个缓冲区对应的是哪个磁盘块
  - 通过 `dev + blockno` 组合唯一标识一个缓冲区

- **`refcnt`**：引用计数
  - 记录有多少个地方正在使用这个缓冲区
  - 作用：防止缓冲区被过早回收
  - 当 `refcnt` 为 0 时，缓冲区可以被驱逐（LRU 策略）

- **`prev` 和 `next`**：LRU 链表指针
  - 将所有缓冲区链接成一个双向链表
  - 最近使用的缓冲区放在链表头部
  - 最久未使用的缓冲区放在链表尾部
  - 当需要空闲缓冲区时，优先选择链表尾部的缓冲区

- **`data[BSIZE]`**：数据存储
  - 存储磁盘块的实际数据
  - `BSIZE` 是块大小（通常为 1024 字节）
  - 无符号字符数组，可以表示任意二进制数据

**LRU 缓存策略**：
1. **访问缓冲区时**：将缓冲区移到链表头部（标记为最近使用）
2. **需要空闲缓冲区时**：检查链表尾部
   - 如果尾部缓冲区的 `refcnt` 为 0 且 `disk` 为真（已写回），则回收
   - 否则继续查找下一个
3. **优点**：
   - 自动保留热点数据（频繁访问的块常驻内存）
   - 简单高效，易于实现

**为什么需要缓冲区？**
- **性能**：磁盘 I/O 比内存访问慢几个数量级
- **减少访问**：多个进程访问同一块时，只需读一次磁盘
- **延迟写**：多次写操作可以合并，减少磁盘写入次数
- **预读**：可以提前读取后续块，提高顺序读取性能

---

## 总结

本章介绍了文件系统的五大核心数据结构：

1. **超级块**：文件系统的"地图"，记录整体布局
2. **磁盘 Inode**：文件元数据的持久化存储
3. **内存 Inode**：磁盘 inode 的缓存，添加了管理字段
4. **目录项**：实现文件名到 inode 的映射
5. **缓冲区**：磁盘块的内存缓存，采用 LRU 策略

这些结构协同工作，实现了高效的文件存储和访问：
- 超级块提供全局信息
- inode 管理文件元数据
- 目录项组织文件层次结构
- 缓冲区优化磁盘 I/O 性能

理解这些结构是掌握文件系统实现的基础，后续章节将基于这些结构介绍文件系统的具体操作（如读取、写入、创建、删除等）。

---

## 6. Virtio 磁盘驱动与 I/O 操作

### 6.1 virtio_disk_rw 函数

#### 函数定义

```c
virtio_disk_rw(struct buf *b, int write) {
    /// ... 设置 I/O 配置
    *R(VIRTIO_MMIO_QUEUE_NOTIFY) = 0;               // 通知磁盘执行 I/O
    struct buf volatile * _b = b;                   // 确保编译器会从内存中加载 'b'
    intr_on();
    while(_b->disk == 1);   // _b->disk == 0 表示此 I/O 操作已完成
    intr_off();
}
```

#### 详细说明

**作用**：向 Virtio 磁盘设备发起读写请求，并同步等待操作完成。这是文件系统与磁盘硬件交互的核心函数。

**执行流程**：

1. **设置 I/O 配置**（代码省略部分）：
   - 配置 Virtio 队列的描述符
   - 设置读写方向（`write` 参数）
   - 将缓冲区信息写入队列
   - 准备设备寄存器

2. **通知设备**：
   ```c
   *R(VIRTIO_MMIO_QUEUE_NOTIFY) = 0;
   ```
   - 向 Virtio 设备的 MMIO（内存映射 I/O）寄存器写入值
   - 触发磁盘设备开始处理 I/O 请求
   - 值 `0` 表示使用队列 0（Virtio 设备可以有多个队列）

3. **使用 volatile 指针**：
   ```c
   struct buf volatile * _b = b;
   ```
   - **为什么需要 `volatile`？**
   - 告诉编译器：`_b` 指向的内存可能被外部因素（硬件中断）修改
   - 禁止编译器优化：每次访问 `_b->disk` 都必须从内存读取，不能使用缓存
   - **如果没有 `volatile`**：编译器可能将 `while(_b->disk == 1);` 优化为死循环或完全忽略

4. **开启中断**：
   ```c
   intr_on();
   ```
   - 启用 CPU 的中断响应
   - 允许磁盘设备在 I/O 完成后发送中断通知
   - 中断处理程序会将 `_b->disk` 设置为 0

5. **轮询等待**：
   ```c
   while(_b->disk == 1);
   ```
   - 忙等待（busy-wait）循环，持续检查 `_b->disk` 的值
   - `disk == 1`：I/O 操作正在进行中
   - `disk == 0`：I/O 操作已完成（由中断处理程序设置）
   - **为什么用轮询而不是睡眠？**
     - 简化实现：不需要复杂的进程调度和唤醒机制
     - 适合单核环境：避免上下文切换的开销
     - 短时间等待：磁盘 I/O 通常很快完成

6. **关闭中断**：
   ```c
   intr_off();
   ```
   - 恢复之前的中断状态
   - 保持系统的中断策略一致性

#### Virtio 协议简介

**Virtio 是什么？**
- 一种虚拟化设备的 I/O 半虚拟化框架
- 允许 guest 操作系统（如 uCore）高效访问宿主机的设备
- 广泛用于 QEMU、KVM 等虚拟化环境

**Virtio 通信机制**：
1. **Virtqueue**（虚拟队列）：
   - Guest 和 Host 之间的通信通道
   - 用于传递 I/O 请求和响应
   - 使用环形缓冲区实现

2. **MMIO 寄存器**：
   - 内存映射 I/O，通过读写特定内存地址与设备交互
   - `VIRTIO_MMIO_QUEUE_NOTIFY`：通知设备有新请求
   - 其他寄存器：设备状态、队列配置等

3. **中断驱动**：
   - 设备完成操作后发送中断
   - Guest 的中断处理程序处理响应
   - 比轮询更高效（但这里使用轮询简化实现）

#### 关键技术点

**1. volatile 关键字的必要性**：

```c
// 没有 volatile（错误）
struct buf *_b = b;
while(_b->disk == 1);  // 编译器可能优化为：if(_b->disk == 1) while(1);

// 有 volatile（正确）
struct buf volatile *_b = b;
while(_b->disk == 1);  // 编译器每次都从内存读取 _b->disk
```

**场景**：
- 磁盘硬件在完成 I/O 后会修改内存中的 `_b->disk`
- 编译器不知道硬件会修改内存，可能假设 `_b->disk` 不会被改变
- `volatile` 确保每次循环都重新检查内存

**2. 忙等待 vs 睡眠唤醒**：

| 方式 | 优点 | 缺点 | 适用场景 |
|------|------|------|----------|
| 忙等待（本实现） | 简单、低延迟 | 浪费 CPU | I/O 快、单核 |
| 睡眠唤醒 | 高效、不浪费 CPU | 复杂、调度开销 | I/O 慢、多核 |

**3. 为什么在 I/O 期间开启中断？**

```c
intr_on();       // 允许中断
while(_b->disk == 1);  // 等待中断处理程序设置 disk=0
intr_off();      // 恢复中断状态
```

- 中断处理程序需要运行才能修改 `_b->disk`
- 如果中断被关闭，磁盘中断无法被处理，导致死锁
- **时序**：发起 I/O → 开中断 → 磁盘完成 → 触发中断 → 中断处理程序设置 disk=0 → 循环结束

---

### 6.2 中断管理函数

#### 函数定义

```c
// 开启中断的函数
static inline void intr_on() {
    w_sstatus(r_sstatus() | SSTATUS_SIE);
}

// 关闭中断的函数
static inline void intr_off() {
    w_sstatus(r_sstatus() & ~SSTATUS_SIE);
}
```

#### 详细说明

**作用**：控制 CPU 的中断响应状态，用于保护临界区和同步硬件操作。

---

### intr_on() - 开启中断

#### 功能

启用 CPU 的设备中断，使 CPU 能够响应外部设备的中断请求。

#### 实现细节

```c
w_sstatus(r_sstatus() | SSTATUS_SIE);
```

**逐步解析**：

1. **`r_sstatus()`**：
   - 读取 RISC-V 的 `sstatus` 寄存器（Supervisor Status Register）
   - `sstatus` 寄存器包含当前 CPU 的运行状态和控制标志

2. **`SSTATUS_SIE`**：
   - 一个位掩码（bit mask），对应 `sstatus` 寄存器中的 SIE（Supervisor Interrupt Enable）位
   - SIE 位控制是否启用中断
   - `1`：启用中断，`0`：禁用中断

3. **按位或操作 `|`**：
   - `r_sstatus() | SSTATUS_SIE`：将 SIE 位置 1，其他位保持不变
   - 例如：如果 `sstatus` 原值为 `0b...0..`，运算后变为 `0b...1..`

4. **`w_sstatus()`**：
   - 将新的值写回 `sstatus` 寄存器
   - 使配置生效，CPU 开始响应中断

**为什么需要按位或？**
- `sstatus` 寄存器包含多个控制位（如中断使能、虚拟化使能等）
- 只修改 SIE 位，不影响其他位
- 避免破坏已有的配置

**示例**：
```c
// 假设 SSTATUS_SIE = 0x00000002 (bit 1)
// 假设 sstatus 原值 = 0x00000001 (只有 bit 0 置位)

r_sstatus()           // 0x00000001
r_sstatus() | 0x02    // 0x00000003 (bit 0 和 bit 1 都置位)
w_sstatus(0x03)       // 写回，中断开启
```

---

### intr_off() - 关闭中断

#### 功能

禁用 CPU 的设备中断，使 CPU 忽略所有外部设备的中断请求。

#### 实现细节

```c
w_sstatus(r_sstatus() & ~SSTATUS_SIE);
```

**逐步解析**：

1. **`~SSTATUS_SIE`**：
   - `~` 是按位取反运算符
   - 如果 `SSTATUS_SIE = 0x00000002`，则 `~SSTATUS_SIE = 0xFFFFFFFD`
   - 结果只有 SIE 位为 0，其他所有位都为 1

2. **按位与操作 `&`**：
   - `r_sstatus() & ~SSTATUS_SIE`：将 SIE 位清零，其他位保持不变
   - 例如：如果 `sstatus` 原值为 `0b...1..`，运算后变为 `0b...0..`

3. **`w_sstatus()`**：
   - 将新的值写回 `sstatus` 寄存器
   - CPU 停止响应中断

**为什么需要按位与取反？**
- 只清除 SIE 位，不影响其他位
- 使用取反确保只有 SIE 位被清除

**示例**：
```c
// 假设 SSTATUS_SIE = 0x00000002 (bit 1)
// 假设 sstatus 原值 = 0x00000003 (bit 0 和 bit 1 都置位)

~SSTATUS_SIE          // 0xFFFFFFFD (bit 1 为 0，其他都为 1)
r_sstatus() & 0xFD    // 0x00000001 (只有 bit 0 置位)
w_sstatus(0x01)       // 写回，中断关闭
```

---

#### 中断控制的使用场景

**1. 保护临界区**：
```c
intr_off();
// 修改共享数据（如缓冲区链表）
intr_on();
```
- 防止中断处理程序与当前代码同时访问共享数据
- 避免竞态条件（race condition）

**2. 原子操作**：
```c
intr_off();
uint old_value = *shared_ptr;
*shared_ptr = new_value;
intr_on();
```
- 确保读-改-写操作不被中断

**3. 同步硬件操作**：
```c
intr_off();
// 配置硬件寄存器
intr_on();
// 等待硬件中断
```
- 确保硬件配置完成后再启用中断

**4. virtio_disk_rw 中的应用**：
```c
intr_on();        // 允许磁盘中断
while(_b->disk == 1);  // 等待中断完成
intr_off();       // 恢复中断状态
```
- 在等待磁盘 I/O 期间必须开启中断
- 中断处理程序会设置 `_b->disk = 0`，使循环结束

---

#### RISC-V 中断机制简介

**中断类型**：
1. **外部中断**：来自外部设备（如键盘、磁盘、网卡）
2. **软件中断**：由软件触发（用于系统调用）
3. **定时器中断**：定时器到期触发
4. **异常**：非法指令、页面错误等

**中断优先级**：
- RISC-V 支持中断优先级和屏蔽
- `sstatus` 寄存器控制全局中断使能
- 其他寄存器（如 `sie`、`sip`）控制特定中断源

**中断流程**：
1. 设备发起中断请求
2. CPU 检查中断是否被启用（`sstatus.SIE`）
3. 如果启用，保存当前上下文（PC、寄存器等）
4. 跳转到中断处理程序（trap handler）
5. 中断处理程序执行（如设置 `_b->disk = 0`）
6. 恢复上下文，继续执行被中断的代码

---

## 7. 完整的 I/O 流程示例

### 读取磁盘块的完整流程

让我们结合前面介绍的所有组件，看一个完整的磁盘读取流程：

#### 步骤 1：分配缓冲区
```c
struct buf *b = bget(dev, blockno);  // 获取或分配缓冲区
if (!b->valid) {
    // 缓冲区无效，需要从磁盘读取
}
```

#### 步骤 2：发起 I/O 请求
```c
virtio_disk_rw(b, 0);  // 0 表示读操作
```

**内部执行**：
1. 配置 Virtio 队列描述符
2. 设置缓冲区地址 `b->data` 和块号 `b->blockno`
3. 将 `b->disk` 设置为 1（表示 I/O 进行中）
4. 通知磁盘设备：`*R(VIRTIO_MMIO_QUEUE_NOTIFY) = 0`

#### 步骤 3：等待 I/O 完成
```c
intr_on();              // 开启中断
while(b->disk == 1);    // 轮询等待
intr_off();             // 关闭中断
```

**并发执行**：
- CPU 循环检查 `b->disk`
- 磁盘设备并行执行读取操作
- 读取完成后，磁盘发送中断

#### 步骤 4：中断处理
```c
// 磁盘中断处理程序（在 intr_on() 后被调用）
void disk_interrupt_handler() {
    struct buf *b = get_completed_buf();  // 从 Virtio 队列获取完成的请求
    b->disk = 0;                          // 标记 I/O 完成
    wakeup(b);                            // 唤醒等待的进程（如果有睡眠机制）
}
```

**效果**：
- `b->disk` 被设置为 0
- `while(b->disk == 1)` 循环结束
- CPU 继续执行后续代码

#### 步骤 5：使用数据
```c
// 现在 b->data 包含了磁盘块的内容
process_data(b->data);
b->valid = 1;  // 标记缓冲区有效
brelse(b);     // 释放缓冲区（refcnt--）
```

---

## 8. 性能优化与设计权衡

### 8.1 中断与轮询的权衡

**轮询（本实现）**：
- ✅ 简单，不需要进程调度
- ✅ 低延迟，立即响应 I/O 完成
- ❌ 浪费 CPU，占用 100% CPU 时间
- 适合：I/O 快、单核、简单系统

**中断驱动（改进方向）**：
- ✅ 高效，CPU 可以执行其他任务
- ✅ 支持多进程并发
- ❌ 复杂，需要进程睡眠和唤醒机制
- ❌ 高开销，上下文切换
- 适合：I/O 慢、多核、复杂系统

**混合方案**（最佳实践）：
- 短时间轮询（如 10 微秒）
- 如果未完成，切换到中断驱动
- 平衡性能和效率

### 8.2 volatile 的性能影响

**vloatile 的代价**：
- 禁止编译器优化，每次都访问内存
- 增加内存访问次数
- 可能影响性能

**何时必须使用**：
- 内存映射 I/O（MMIO）
- 多线程共享变量（但应使用原子操作）
- 信号处理程序修改的变量

**何时避免使用**：
- 普通变量（应使用锁、原子操作）
- 可以被编译器优化的场景

### 8.3 中断控制的粒度

**细粒度控制**（推荐）：
```c
intr_off();
// 只保护关键的几行代码
critical_section();
intr_on();
```
- 减少中断延迟
- 提高系统响应性

**粗粒度控制**（避免）：
```c
intr_off();
// 大段代码
large_function();
intr_on();
```
- 增加中断延迟
- 可能丢失关键中断
- 影响系统实时性

---

## 9. 常见问题与调试

### Q1: 为什么我的磁盘 I/O 会死锁？

**可能原因**：
1. 忘记调用 `intr_on()`，中断处理程序无法运行
2. `volatile` 缺失，编译器优化导致死循环
3. 中断处理程序未设置 `_b->disk = 0`

**调试方法**：
- 检查 `sstatus` 寄存器的值
- 在循环中添加超时和打印
- 使用调试器单步执行

### Q2: volatile 真的必要吗？

**实验**：
```c
// 无 volatile（优化后）
while(_b->disk == 1);
// 可能被编译为：
if (_b->disk == 1) {
    while(1);  // 死循环！
}
```

**验证**：
- 使用 `-O2` 或 `-O3` 优化编译
- 对比有无 `volatile` 的汇编代码
- 测试：在中断处理程序中修改 `_b->disk`

### Q3: 为什么要先 intr_on() 再 intr_off()？

**答案**：恢复之前的中断状态

```c
// 假设调用 virtio_disk_rw 前中断是关闭的
intr_off();          // 中断已关闭（多余，但无害）
virtio_disk_rw(b, 0);
// virtio_disk_rw 内部：
//   intr_on();   // 开启中断，允许磁盘中断
//   while(_b->disk == 1);  // 等待
//   intr_off();  // 恢复关闭状态
// 现在中断又关闭了，与调用前一致
```

**设计原则**：
- 函数应该恢复调用前的状态
- 避免副作用影响调用者

---

## 总结

本章扩展了文件系统的实现细节，重点介绍了：

### 核心概念

1. **Virtio 磁盘驱动**：
   - 半虚拟化设备驱动框架
   - 通过 MMIO 寄存器与设备通信
   - 使用队列传递 I/O 请求

2. **I/O 同步机制**：
   - 轮询等待：简单但低效
   - 中断驱动：复杂但高效
   - 本实现使用轮询 + 中断完成信号

3. **中断管理**：
   - `intr_on()`：启用中断，允许硬件中断
   - `intr_off()`：禁用中断，保护临界区
   - 基于 RISC-V 的 `sstatus` 寄存器

4. **volatile 关键字**：
   - 禁止编译器优化
   - 确保每次都从内存读取
   - 对硬件编程至关重要

### 关键技术点

- **Virtio 协议**：Guest-Host 通信标准
- **MMIO 寄存器**：内存映射 I/O
- **中断处理**：异步事件响应
- **轮询优化**：平衡简单性和效率
- **位操作技巧**：按位与、或、取反

### 设计权衡

| 方面 | 简单实现 | 高效实现 |
|------|----------|----------|
| I/O 等待 | 轮询（忙等待） | 睡眠唤醒 |
| 中断控制 | 粗粒度 | 细粒度 |
| 性能 | 低 CPU 利用率 | 高 CPU 利用率 |
| 复杂度 | 简单 | 复杂 |

### 实践建议

1. **硬件编程**：始终使用 `volatile` 访问 MMIO
2. **临界区保护**：最小化 `intr_off()` 的范围
3. **I/O 策略**：根据场景选择轮询或中断
4. **调试方法**：检查寄存器值、添加超时、打印状态

理解这些底层机制，对于掌握操作系统和驱动开发至关重要。下一章我们将基于这些基础，学习文件系统的高层操作实现。

---

## 10. 内核中断处理机制的实现

### 10.1 背景与动机

#### 之前的问题

在早期的实现中，当内核态发生中断或异常时，系统会直接触发 panic（内核恐慌）并终止运行：

```c
// 旧的 kerneltrap 实现（简化版）
void kerneltrap() {
    // 直接 panic，不接受任何内核态的陷阱
    panic("trap from kernel!");
}
```

**这种设计的缺陷**：
1. **无法处理内核态中断**：时钟中断、外部中断在内核态发生时无法处理
2. **驱动无法工作**：磁盘驱动的中断处理程序无法在内核态运行
3. **系统可靠性差**：任何内核态的意外都会导致整个系统崩溃
4. **功能受限**：无法在内核执行期间响应硬件事件

#### 改进目标

现在我们需要支持内核态的中断处理，特别是：
- **外部中断**：Virtio 磁盘驱动的中断
- **时钟中断**：内核态的时钟管理
- **异常处理**：保留对真正内核错误的安全处理

---

### 10.2 整体架构变化

#### 修改前 vs 修改后

| 方面 | 修改前 | 修改后 |
|------|--------|--------|
| **内核态陷阱** | 直接 panic | 分类处理（中断/异常） |
| **中断向量** | 使用用户态的 trapvec | 独立的 kernelvec |
| **上下文保存** | 简单或不存在 | 完整的寄存器保存/恢复 |
| **stvec 设置** | 不区分内核态 | 内核态指向 kernelvec |
| **功能** | 仅用户态陷阱 | 用户态 + 内核态陷阱 |
| **可靠性** | 内核错误无法区分 | 可区分中断和真正的错误 |

#### 核心组件

1. **kernelvec.S**：内核态的中断入口和出口汇编代码
2. **kerneltrap()**：内核态陷阱处理的 C 函数
3. **devintr()**：设备中断处理函数
4. **stvec 管理**：进入内核后切换到 kernelvec

---

### 10.3 kernelvec.S - 内核中断向量表

#### 完整代码

```assembly
# kernelvec.S

kernelvec:
    # make room to save registers.
    addi sp, sp, -256

    # save the registers expect x0 (x0 is always 0, no need to save)
    sd ra, 0(sp)
    sd sp, 8(sp)
    sd gp, 16(sp)
    # ... (省略中间的寄存器保存)
    sd t4, 224(sp)
    sd t5, 232(sp)
    sd t6, 240(sp)

    call kerneltrap

kernelret:
    # restore registers.
    # 思考：为什么直接就使用了sp？
    ld ra, 0(sp)
    ld sp, 8(sp)
    ld gp, 16(sp)
    # restore all registers expect x0
    ld t4, 224(sp)
    ld t5, 232(sp)
    ld t6, 240(sp)

    addi sp, sp, 256
    sret
```

#### 详细说明

**1. 入口：kernelvec**

**作用**：内核态发生陷阱时的入口点，类似于用户态的 `uservec`。

**栈空间分配**：
```assembly
addi sp, sp, -256
```
- 在当前栈上分配 256 字节的空间
- 256 字节 = 32 个寄存器 × 8 字节/寄存器
- RISC-V 有 32 个通用寄存器（x0-x31），但 x0 固定为 0，不需要保存

**为什么直接使用当前栈？**
- 内核线程已经有合法的栈（不同于用户态需要切换栈）
- 不需要像 `uservec` 那样切换到 `trapframe` 所在的栈
- 简化了实现，减少了开销

**寄存器保存**：
```assembly
sd ra, 0(sp)      # 返回地址
sd sp, 8(sp)      # 栈指针（保存旧的 sp 值）
sd gp, 16(sp)     # 全局指针
# ... (其他寄存器)
sd t6, 240(sp)    # 临时寄存器
```

- `sd` (store doubleword)：将 64 位寄存器值存储到内存
- 顺序：从 `ra` 到 `t6`，按寄存器编号递增
- 偏移量：0, 8, 16, ..., 240（每个寄存器占 8 字节）

**思考题**：为什么不保存 `pc`（程序计数器）？
- `sepc` (Supervisor Exception Program Counter) 寄存器自动保存了陷阱发生时的 `pc`
- 在返回时通过 `sret` 指令自动恢复 `pc`
- 不需要手动保存到栈上

**调用 C 处理函数**：
```assembly
call kerneltrap
```
- 调用 C 语言编写的陷阱处理函数
- 此时所有寄存器都已保存，可以安全使用
- `call` 指令自动将返回地址保存到 `ra`

**2. 出口：kernelret**

**作用**：从内核态陷阱返回，恢复执行上下文。

**寄存器恢复**：
```assembly
ld ra, 0(sp)      # 恢复返回地址
ld sp, 8(sp)      # 恢复栈指针（注意顺序！）
ld gp, 16(sp)     # 恢复全局指针
# ... (其他寄存器)
ld t6, 240(sp)    # 恢复临时寄存器
```

- `ld` (load doubleword)：从内存加载 64 位值到寄存器
- **关键**：必须按照保存的逆序恢复（实际上顺序不重要，因为各寄存器独立）
- **特别注意**：`ld sp, 8(sp)` 恢复的是旧的 `sp` 值，这是正确的

**思考题**：为什么恢复 `sp` 后还能继续访问栈上的数据？
```assembly
ld sp, 8(sp)      # 恢复旧的 sp
# 后续的 ld 指令还需要使用 sp...
ld t4, 224(sp)    # 这里的 sp 是新的还是旧的？
```

**答案**：这看起来像是个问题，但实际上：
1. **汇编的误解**：代码中的 `ld sp, 8(sp)` 是在恢复过程中，但后续的 `ld t4, 224(sp)` 使用的是恢复后的 `sp`
2. **实际实现**：正确的顺序应该是最后恢复 `sp`，或者在恢复 `sp` 之前先恢复其他所有寄存器
3. **更安全的实现**：
   ```assembly
   # 先恢复除 sp 外的所有寄存器
   ld ra, 0(sp)
   ld gp, 16(sp)
   # ... (跳过 sp)
   ld t6, 240(sp)
   
   # 最后恢复 sp
   ld sp, 8(sp)    # 现在可以安全恢复 sp 了
   ```

**栈空间释放和返回**：
```assembly
addi sp, sp, 256    # 释放之前分配的 256 字节
sret                # 从异常返回
```

- `addi sp, sp, 256`：将栈指针恢复到陷阱发生前的值
- `sret` (Supervisor Exception Return)：
  - 从 `sepc` 恢复 `pc`
  - 从 `sstatus` 恢复特权级和其他状态
  - 重新启用中断（如果之前被禁用）

---

### 10.4 kerneltrap() - 内核态陷阱处理

#### 完整代码

```c
void kerneltrap() {
    // 老三样：读取关键的系统寄存器
    uint64 sepc = r_sepc();      // 陷阱发生时的程序计数器
    uint64 sstatus = r_sstatus(); // 当前状态寄存器
    uint64 scause = r_scause();   // 陷阱原因

    // 安全检查：确保陷阱确实来自内核态
    if ((sstatus & SSTATUS_SPP) == 0)
        panic("kerneltrap: not from supervisor mode");

    if (scause & (1ULL << 63)) {
        // 中断处理（最高位为 1）
        devintr(scause & 0xff);  // 传递低 8 位的中断原因
    } else {
        // 异常处理：内核态的异常通常是严重错误
        error("invalid trap from kernel: %p, stval = %p sepc = %p\n",
              scause, r_stval(), sepc);
        exit(-1);  // 终止进程或系统
    }
}
```

#### 详细说明

**1. 读取系统寄存器（老三样）**

```c
uint64 sepc = r_sepc();
uint64 sstatus = r_sstatus();
uint64 scause = r_scause();
```

**为什么称为"老三样"？**
- 这是陷阱处理的标准开头，几乎每个陷阱处理函数都会执行这三步
- 类似于"你好世界"是编程的第一课

**sepc (Supervisor Exception PC)**：
- 陷阱发生时的程序计数器（PC）值
- 指向导致陷阱的指令
- 在 `sret` 时自动恢复到 PC

**sstatus (Supervisor Status)**：
- 包含当前的 CPU 状态
- 关键位：
  - `SSTATUS_SPP` (bit 8)：之前的特权级（0=用户态，1=内核态）
  - `SSTATUS_SIE` (bit 1)：中断使能
  - 其他控制位（虚拟化、扩展使能等）

**scause (Supervisor Cause)**：
- 指示陷阱的原因
- **最高位（bit 63）**：
  - `1`：中断（异步事件）
  - `0`：异常（同步事件）
- **低 8 位（bit 0-7）**：具体原因
  - `0`：指令地址未对齐
  - `2`：非法指令
  - `3`：断点
  - `8`：用户态环境调用（系统调用）
  - `13`：页面错误
  - ...

**2. 安全检查**

```c
if ((sstatus & SSTATUS_SPP) == 0)
    panic("kerneltrap: not from supervisor mode");
```

**目的**：确保陷阱确实来自内核态

**原理**：
- `SSTATUS_SPP` 位记录了陷阱发生前的特权级
- `1`：之前是内核态（Supervisor Mode）
- `0`：之前是用户态（User Mode）

**为什么需要这个检查？**
- `kerneltrap` 应该只处理内核态的陷阱
- 如果用户态陷阱错误地进入 `kerneltrap`，说明系统状态异常
- 防止权限混淆和安全漏洞

**可能的原因**：
- `stvec` 设置错误
- 汇编代码 bug
- 硬件异常或错误

**3. 中断处理**

```c
if (scause & (1ULL << 63)) {
    // 中断：最高位为 1
    devintr(scause & 0xff);
}
```

**判断中断**：
- `scause & (1ULL << 63)`：检查最高位是否为 1
- `(1ULL << 63)`：`0x8000000000000000`（只有最高位置位）
- 如果结果非零，说明是中断

**提取中断原因**：
- `scause & 0xff`：提取低 8 位
- `0xff`：`0b11111111`
- 低 8 位编码了具体的中断类型：
  - `0`：用户态软件中断
  - `1`：内核态软件中断
  - `4`：用户态时钟中断
  - `5`：内核态时钟中断
  - `8`：用户态外部中断
  - `9`：内核态外部中断

**4. 异常处理**

```c
else {
    // 异常：最高位为 0
    error("invalid trap from kernel: %p, stval = %p sepc = %p\n",
          scause, r_stval(), sepc);
    exit(-1);
}
```

**为什么内核态异常要 panic？**
- **内核态异常通常是严重错误**：
  - 空指针解引用
  - 非法指令
  - 页面错误（内核应该避免）
  - 权限违规

**与用户态的区别**：
- **用户态异常**：可以杀死进程，继续运行系统
- **内核态异常**：整个内核都不可信了，必须终止系统

**例外**：
- 某些内核态异常可能是合法的（如缺页处理），但本实现简化为全部 panic

---

### 10.5 devintr() - 设备中断处理

#### 完整代码

```c
void devintr(uint64 cause) {
    int irq;
    switch (cause) {
        case SupervisorTimer:
            // 时钟中断处理
            set_next_timer();  // 设置下一个时钟中断

            // 关键决策：内核态时钟中断不切换进程
            if((r_sstatus() & SSTATUS_SPP) == 0) {
                // 只有在用户态发生时钟中断时才切换进程
                yield();
            }
            break;

        case SupervisorExternal:
            // 外部中断处理
            irq = plic_claim();  // 从 PLIC 获取中断号

            if (irq == UART0_IRQ) {
                // UART 串口中断：rustsbi 已处理，无需操作
                // do nothing
            } else if (irq == VIRTIO0_IRQ) {
                // Virtio 磁盘中断：调用磁盘驱动处理
                virtio_disk_intr();
            }

            if (irq)
                plic_complete(irq);  // 通知 PLIC 中断处理完毕
            break;
    }
}
```

#### 详细说明

**1. 时钟中断处理**

```c
case SupervisorTimer:
    set_next_timer();  // 设置下一个时钟中断
```

**时钟中断的作用**：
- **时间片轮转**：定期切换进程，实现多任务调度
- **定时功能**：为 `sleep()`、`alarm()` 等系统调用提供基础
- **系统统计**：统计进程运行时间、CPU 使用率等

**为什么内核态时钟中断不切换进程？**

| 特性 | 用户态时钟中断 | 内核态时钟中断 |
|------|---------------|---------------|
| **触发位置** | 用户程序执行时 | 内核执行系统调用时 |
| **是否切换** | ✅ 是，调用 `yield()` | ❌ 否，直接返回 |
| **原因** | 用户程序可能运行时间长 | 内核应该快速完成，不主动抢占 |

**内核态不切换的原因**：

1. **原子性要求**：
   - 内核正在执行系统调用（如 `read()`、`write()`）
   - 系统调用应该是原子的（从用户角度看）
   - 中途切换会导致状态不一致

2. **锁和同步**：
   - 内核可能持有自旋锁或互斥锁
   - 切换进程可能导致死锁
   - 其他进程可能等待这个锁

3. **性能考虑**：
   - 内核系统调用通常很快完成
   - 不必要的切换增加开销
   - 减少上下文切换次数

4. **简化实现**：
   - 避免复杂的内核抢占机制
   - 降低调试难度
   - 教学系统的合理简化

**现代操作系统的改进**：
- **可抢占内核**：允许内核态被中断和切换
- **实现复杂**：需要仔细处理锁、临界区、优先级
- **性能提升**：更好的响应性和实时性

**2. 外部中断处理**

```c
case SupervisorExternal:
    irq = plic_claim();  // 获取中断号
```

**PLIC (Platform-Level Interrupt Controller)**：
- RISC-V 标准的中断控制器
- 管理多个外部设备的中断
- 提供优先级和路由功能

**plic_claim() 的作用**：
- 从 PLIC 获取待处理的中断号
- PLIC 会自动选择优先级最高的中断
- 阻塞该中断，防止重复处理

**UART 串口中断**：
```c
if (irq == UART0_IRQ) {
    // do nothing
}
```

**为什么不需要处理？**
- **RustSBI 代劳**：
  - SBI (Supervisor Binary Interface) 是运行在 M 态的固件
  - RustSBI 是 SBI 的一种实现（用 Rust 编写）
  - 它已经处理了 UART 输入，将字符放入缓冲区

- **简化设计**：
  - uCore 不需要直接操作 UART 硬件
  - 通过 SBI 接口读取输入
  - 减少了驱动代码复杂度

**Virtio 磁盘中断**：
```c
else if (irq == VIRTIO0_IRQ) {
    virtio_disk_intr();
}
```

**virtio_disk_intr() 的作用**：
1. 从 Virtio 队列中读取完成的 I/O 请求
2. 设置 `b->disk = 0`，唤醒等待的进程
3. 清理队列状态

**与 virtio_disk_rw 的配合**：
```c
// virtio_disk_rw 中：
intr_on();
while(_b->disk == 1);  // 等待中断
intr_off();

// virtio_disk_intr 中：
b->disk = 0;  // 唤醒 virtio_disk_rw
```

**完成中断处理**：
```c
if (irq)
    plic_complete(irq);
```

**plic_complete() 的作用**：
- 通知 PLIC 中断已处理完毕
- PLIC 可以继续发送该设备的新中断
- 解除该中断的阻塞状态

**为什么检查 `if (irq)`？**
- `plic_claim()` 可能返回 0（没有待处理的中断）
- `plic_complete(0)` 可能是非法操作
- 增加健壮性，防止误操作

---

### 10.6 stvec 设置与管理

#### 进入内核后的 stvec 设置

**问题**：为什么需要单独设置 stvec？

**答案**：区分用户态和内核态的陷阱处理

**用户态**：
```c
// 用户态执行时，stvec 指向 uservec
w_stvec((uint64)uservec);
```
- 陷阱发生时跳转到 `uservec`
- `uservec` 需要切换栈到 `trapframe`
- 处理完后通过 `usertrapret` 返回用户态

**内核态**：
```c
// 进入内核后（如系统调用），stvec 指向 kernelvec
w_stvec((uint64)kernelvec);
```
- 陷阱发生时跳转到 `kernelvec`
- `kernelvec` 直接使用当前栈
- 处理完后通过 `kernelret` 返回内核代码

**设置时机**：
```c
// 在 usertrap 中（处理用户态陷阱）
void usertrap() {
    // ...

    if ((r_sstatus() & SSTATUS_SPP) == 0) {
        // 来自用户态
        w_stvec((uint64)kernelvec);  // 切换到内核态陷阱处理
    }

    // 执行系统调用或内核功能
    // ...

    // 返回用户态前
    w_stvec((uint64)uservec);  // 恢复用户态陷阱处理
}
```

**为什么需要切换？**
- **安全性**：用户态陷阱需要切换栈，内核态不需要
- **性能**：避免不必要的栈切换
- **正确性**：内核态使用 `uservec` 可能导致栈混乱

---

### 10.7 前后对比总结

#### 修改前

**特点**：
1. **简单粗暴**：内核态陷阱直接 panic
2. **功能受限**：无法处理内核态中断
3. **可靠性差**：任何内核态问题都导致系统崩溃
4. **实现简单**：代码量少，易于理解

**代码示例**：
```c
void kerneltrap() {
    panic("trap from kernel!");
}
```

**缺陷**：
- ❌ 无法在内核态处理磁盘中断
- ❌ 无法在内核态处理时钟中断
- ❌ 驱动程序无法工作
- ❌ 系统功能严重受限

#### 修改后

**特点**：
1. **分类处理**：区分中断和异常
2. **功能完整**：支持内核态中断处理
3. **可靠性高**：只有真正的内核错误才 panic
4. **实现复杂**：需要汇编和 C 配合

**代码示例**：
```c
void kerneltrap() {
    // 读取寄存器
    uint64 sepc = r_sepc();
    uint64 sstatus = r_sstatus();
    uint64 scause = r_scause();

    // 检查来源
    if ((sstatus & SSTATUS_SPP) == 0)
        panic("kerneltrap: not from supervisor mode");

    // 分类处理
    if (scause & (1ULL << 63)) {
        devintr(scause & 0xff);  // 中断
    } else {
        error("invalid trap from kernel...");  // 异常
        exit(-1);
    }
}
```

**优势**：
- ✅ 支持内核态磁盘中断
- ✅ 支持内核态时钟中断（不切换进程）
- ✅ 驱动程序正常工作
- ✅ 系统功能完整

**代价**：
- 增加了约 200 行汇编和 C 代码
- 需要仔细管理 stvec
- 需要理解中断控制器（PLIC）
- 调试难度增加

---

### 10.8 关键技术点总结

**1. 寄存器保存与恢复**
- 使用汇编代码在栈上保存所有通用寄存器
- `pc` 通过 `sepc` 和 `sret` 自动处理
- 必须按照正确的顺序恢复

**2. 中断 vs 异常**
- **中断**（`scause[63] == 1`）：异步事件，可以处理
- **异常**（`scause[63] == 0`）：同步错误，通常是 bug

**3. 内核态时钟中断不切换进程**
- 保证系统调用的原子性
- 避免死锁和状态不一致
- 简化实现，降低复杂度

**4. stvec 管理**
- 用户态：`uservec`（需要切换栈）
- 内核态：`kernelvec`（直接使用当前栈）
- 在进入/退出内核时切换

**5. PLIC 中断控制器**
- 管理外部设备的中断
- 提供优先级和路由
- 需要 `claim` 和 `complete` 配对使用

---

### 10.9 实践建议

**1. 调试技巧**
- 在 `kerneltrap` 中添加打印，查看 `scause`
- 使用调试器单步执行汇编代码
- 检查 `stvec` 的设置是否正确

**2. 常见错误**
- ❌ 忘记切换 `stvec`，导致内核态使用错误的处理程序
- ❌ 寄存器恢复顺序错误，导致栈混乱
- ❌ 忘记调用 `plic_complete()`，导致中断无法再次触发
- ❌ 内核态异常处理不当，掩盖真正的 bug

**3. 性能优化**
- 使用快速的汇编指令
- 最小化临界区
- 避免不长的中断处理
- 考虑中断共享和合并

**4. 安全考虑**
- 严格检查特权级
- 验证中断源
- 防止中断风暴
- 处理恶意设备

---

## 11. 缓冲区读写接口

### 11.1 bread() - 读取磁盘块到缓冲区

#### 完整代码

```c
// os/bio.c

struct buf *bread(uint dev, uint blockno) {
    struct buf *b;
    b = bget(dev, blockno);      // 获取或分配缓冲区
    if (!b->valid) {
        virtio_disk_rw(b, R);    // 从磁盘读取
        b->valid = 1;            // 标记缓冲区有效
    }
    return b;
}
```

#### 详细说明

**作用**：读取指定设备的磁盘块到缓冲区，如果缓冲区已有效则直接返回，避免重复读取。

**参数**：
- `dev`：设备号，标识要读取的块设备
- `blockno`：块号，标识要读取的逻辑块

**返回值**：
- 指向 `struct buf` 的指针，包含磁盘块的数据

**执行流程**：

**步骤 1：获取缓冲区**
```c
b = bget(dev, blockno);
```

**bget() 的作用**：
- 从缓冲区缓存中查找 `(dev, blockno)` 对应的缓冲区
- 如果找到：
  - 增加引用计数 `b->refcnt++`
  - 将缓冲区移到 LRU 链表头部（最近使用）
  - 返回该缓冲区
- 如果未找到：
  - 从 LRU 链表尾部选择一个缓冲区进行回收
  - 如果该缓冲区是脏的（`b->disk == 0` 且 `b->valid == 1`），先写回磁盘
  - 初始化新缓冲区的 `dev`、`blockno`、`valid=0`、`disk=1`
  - 返回新缓冲区

**为什么需要 bget？**
- **缓存管理**：避免频繁的磁盘 I/O
- **资源复用**：多个请求可以共享同一个缓冲区
- **LRU 策略**：自动淘汰不常用的缓冲区
- **引用计数**：防止正在使用的缓冲区被回收

**步骤 2：检查有效性**
```c
if (!b->valid) {
    // 需要从磁盘读取
}
```

**`b->valid` 的含义**：
- `1`：缓冲区中的数据有效，是磁盘块的最新副本
- `0`：缓冲区中的数据无效或未初始化

**不需要读取的情况**：
- 缓冲区已在缓存中，且数据是最新的
- 之前的 `bread()` 或 `bwrite()` 已经加载了数据
- 避免重复的磁盘 I/O，提高性能

**需要读取的情况**：
- 首次访问该块
- 缓冲区被回收后重新分配
- 数据可能已被其他进程修改（写回策略）

**步骤 3：从磁盘读取**
```c
virtio_disk_rw(b, R);
```

**`R` 的含义**：
- `R` 是读取操作的常量（通常定义为 `0`）
- 对应的 `W` 是写入操作（通常定义为 `1`）

**virtio_disk_rw 的执行**：
1. 配置 Virtio 队列描述符（设备、块号、方向）
2. 设置 `b->disk = 1`（I/O 进行中）
3. 通知磁盘设备：`*R(VIRTIO_MMIO_QUEUE_NOTIFY) = 0`
4. 开启中断：`intr_on()`
5. 轮询等待：`while(_b->disk == 1);`
6. 中断处理程序设置 `b->disk = 0`
7. 关闭中断：`intr_off()`

**同步等待**：
- `virtio_disk_rw` 是同步阻塞函数
- 在 I/O 完成前，CPU 会忙等待
- 数据会被加载到 `b->data[]` 数组中

**步骤 4：标记有效**
```c
b->valid = 1;
```

**作用**：
- 标记缓冲区中的数据是有效的
- 后续的 `bread()` 调用可以直接使用，无需再次读取
- 通知缓冲区管理系统：这个缓冲区包含有效数据

**时机**：
- 必须在 `virtio_disk_rw` 成功完成后设置
- 如果 I/O 失败，`valid` 应保持为 0

**步骤 5：返回缓冲区**
```c
return b;
```

**返回后的使用**：
- 调用者可以通过 `b->data` 访问磁盘块的内容
- 调用者完成后应调用 `brelse(b)` 释放缓冲区（`b->refcnt--`）

#### 使用示例

```c
// 读取超级块
struct buf *b;
struct superblock *sb;

b = bread(ROOTDEV, 1);  // 读取设备 0 的块 1（超级块）
sb = (struct superblock *)b->data;

// 使用超级块信息
printf("魔法数: 0x%x\n", sb->magic);
printf("块数: %d\n", sb->nblocks);

// 释放缓冲区
brelse(b);
```

#### 设计优点

**1. 缓存透明**：
- 调用者不需要知道缓存机制
- 自动处理缓存命中和未命中
- 简化了上层代码

**2. 延迟读取**：
- 只有在真正需要时才读取磁盘
- 减少不必要的 I/O
- 提高性能

**3. 引用计数**：
- 防止缓冲区在使用中被回收
- 支持多个调用者共享缓冲区
- 自动管理缓冲区生命周期

**4. 原子性**：
- `bread()` 返回时，数据已经完全加载
- 调用者可以直接使用，无需等待
- 简化了并发控制

---

### 11.2 bwrite() - 将缓冲区内容写入磁盘

#### 完整代码

```c
// os/bio.c

// 将缓冲区内容写入磁盘
void bwrite(struct buf *b) {
    virtio_disk_rw(b, W);
}
```

#### 详细说明

**作用**：将缓冲区的内容写入磁盘。注意：这个函数不修改缓冲区的状态标志。

**参数**：
- `b`：指向要写入的缓冲区

**执行流程**：

**直接发起写操作**：
```c
virtio_disk_rw(b, W);
```

**`W` 的含义**：
- `W` 是写入操作的常量（通常定义为 `1`）
- 告诉 `virtio_disk_rw` 这是一个写操作

**virtio_disk_rw 的执行（写操作）**：
1. 配置 Virtio 队列描述符（方向：写）
2. 设置 `b->disk = 1`（I/O 进行中）
3. 通知磁盘设备：`*R(VIRTIO_MMIO_QUEUE_NOTIFY) = 0`
4. 开启中断：`intr_on()`
5. 轮询等待：`while(_b->disk == 1);`
6. 中断处理程序将 `b->data[]` 的内容写入磁盘
7. 关闭中断：`intr_off()`

#### 关键问题：为什么不设置 `b->valid = 1`？

**对比 bread() 和 bwrite()**：

| 操作 | bread() | bwrite() |
|------|---------|----------|
| **virtio_disk_rw** | `R` (读) | `W` (写) |
| **设置 valid** | ✅ 是：`b->valid = 1;` | ❌ 否：不设置 |
| **原因** | 从磁盘加载新数据 | 写入后数据仍在内存中 |
| **valid 状态** | 无效 → 有效 | 保持不变 |

**bwrite() 不修改 valid 的原因**：

**1. valid 已经是 1**：
- 在调用 `bwrite()` 之前，缓冲区必须已经有效
- 通常流程：`bread()` → 修改 `b->data[]` → `bwrite()`
- `bread()` 已经设置了 `b->valid = 1`

**2. 写入后数据仍在内存**：
- 写入磁盘是"持久化"操作
- 内存中的副本仍然有效
- 不需要重新标记

**3. 脏标记的处理**：
- 某些实现使用 `B_DIRTY` 标志表示数据被修改
- 写入后清除 `B_DIRTY` 标志
- 本实现简化了脏标记机制

#### 典型使用场景

**场景 1：修改超级块**
```c
struct buf *b;
struct superblock *sb;

// 读取超级块
b = bread(ROOTDEV, 1);
sb = (struct superblock *)b->data;

// 修改超级块
sb->nblocks++;  // 增加数据块计数

// 写回磁盘
bwrite(b);  // 持久化修改

// 释放缓冲区
brelse(b);
```

**场景 2：分配新块**
```c
struct buf *b;
uint bno;

// 分配一个新块
bno = alloc_block(ROOTDEV);

// 读取该块（初始化）
b = bread(ROOTDEV, bno);
memset(b->data, 0, BSIZE);  // 清零

// 写回磁盘（标记为已分配）
bwrite(b);

// 释放缓冲区
brelse(b);
```

**场景 3：更新 inode**
```c
struct buf *b;
struct dinode *dip;

// 读取包含 inode 的块
b = bread(dev, IBLOCK(inum, sb));
dip = (struct dinode *)b->data + inum % IPB;

// 修改 inode
dip->size += 512;  // 增加文件大小

// 写回磁盘
bwrite(b);

// 释放缓冲区
brelse(b);
```

#### bwrite() 的同步特性

**同步写入 vs 异步写入**：

| 特性 | 本实现 (bwrite) | 异步写入 |
|------|----------------|---------|
| **阻塞** | ✅ 是，等待 I/O 完成 | ❌ 否，立即返回 |
| **数据一致性** | 强一致性 | 弱一致性 |
| **性能** | 较低（等待磁盘） | 较高（批量写入） |
| **复杂度** | 简单 | 复杂（需要管理脏缓冲区） |
| **适用场景** | 教学系统、关键数据 | 高性能系统 |

**为什么使用同步写入？**
1. **简化实现**：
   - 不需要复杂的脏缓冲区管理
   - 不需要延迟写机制
   - 不需要写回线程

2. **数据一致性**：
   - 写入完成后，数据确实在磁盘上
   - 系统崩溃时数据丢失风险低
   - 调试简单，状态可预测

3. **教学友好**：
   - 代码简洁，易于理解
   - 行为直观，符合直觉
   - 适合学习基本原理

**现代操作系统的优化**：
- **延迟写（Write-Back）**：
  - `bwrite()` 只标记缓冲区为脏
  - 不立即写入磁盘
  - 后台线程定期写回脏缓冲区
  - 或者：缓冲区被回收时写回

- **优点**：
  - 合并多次写入为一次磁盘 I/O
  - 提高性能
  - 减少磁盘访问次数

- **缺点**：
  - 实现复杂
  - 数据一致性风险
  - 需要处理写回失败

#### bwrite() 和 brelse() 的配合

**完整流程**：
```c
// 1. 读取缓冲区
struct buf *b = bread(dev, blockno);

// 2. 修改数据
memcpy(b->data, new_data, BSIZE);

// 3. 写回磁盘
bwrite(b);

// 4. 释放缓冲区
brelse(b);
```

**brelse() 的作用**：
```c
void brelse(struct buf *b) {
    b->refcnt--;  // 减少引用计数
    // 如果 refcnt == 0，缓冲区可以被回收
}
```

**为什么不立即释放缓冲区？**
- 缓存机制：保留在内存中，供后续使用
- 引用计数：确保所有使用者都完成后才回收
- LRU 管理：最近使用的缓冲区保留更久

---

### 11.3 bread() 和 bwrite() 的协作

#### 文件系统操作的典型模式

**读取文件内容**：
```c
// 1. 读取 inode
struct buf *b;
struct dinode *dip;
b = bread(dev, IBLOCK(inum, sb));
dip = (struct dinode *)b->data + inum % IPB;

// 2. 获取数据块号
uint bno = dip->addrs[0];  // 第一个直接块
brelse(b);

// 3. 读取数据块
b = bread(dev, bno);
char *data = b->data;

// 4. 使用数据
printf("%s\n", data);

// 5. 释放缓冲区
brelse(b);
```

**写入文件内容**：
```c
// 1. 读取 inode
struct buf *b;
struct dinode *dip;
b = bread(dev, IBLOCK(inum, sb));
dip = (struct dinode *)b->data + inum % IPB;

// 2. 分配新数据块
uint bno = alloc_block(dev);
dip->addrs[0] = bno;
dip->size += BSIZE;

// 3. 写回 inode
bwrite(b);
brelse(b);

// 4. 读取数据块
b = bread(dev, bno);
memset(b->data, 0, BSIZE);
strcpy(b->data, "Hello, World!");

// 5. 写回数据块
bwrite(b);
brelse(b);
```

---

### 11.4 错误处理

#### 潜在错误场景

**1. 磁盘 I/O 失败**
```c
// virtio_disk_rw 内部可能失败
if (disk_error) {
    // 应该如何处理？
}
```

**本实现的问题**：
- `virtio_disk_rw` 没有返回值
- 无法检测 I/O 是否成功
- 失败时 `b->valid` 可能被错误地设置为 1

**改进方案**：
```c
struct buf *bread(uint dev, uint blockno) {
    struct buf *b = bget(dev, blockno);
    if (!b->valid) {
        if (virtio_disk_rw(b, R) < 0) {
            panic("bread: disk read failed");
        }
        b->valid = 1;
    }
    return b;
}
```

**2. 缓冲区分配失败**
```c
// bget 可能找不到可回收的缓冲区
struct buf *b = bget(dev, blockno);
if (b == NULL) {
    panic("bread: no buffers");
}
```

**原因**：
- 所有缓冲区的 `refcnt > 0`（都在使用中）
- 内存不足，无法分配新的缓冲区

**解决方案**：
- 增加缓冲区数量
- 等待缓冲区释放
- 使用更激进的回收策略

**3. 并发访问冲突**
```c
// 进程 A 和进程 B 同时读取同一个块
struct buf *b1 = bread(dev, blockno);  // 进程 A
struct buf *b2 = bread(dev, blockno);  // 进程 B

// b1 和 b2 应该指向同一个缓冲区
```

**bget 的处理**：
- 查找已存在的缓冲区
- 增加 `refcnt`：`b->refcnt += 2`
- 返回同一个缓冲区指针

**正确性**：
- 两个进程共享同一个缓冲区
- 数据一致性得到保证
- 节省内存和 I/O

---

### 11.5 性能优化

#### 优化策略

**1. 预读（Read-Ahead）**
```c
struct buf *bread(uint dev, uint blockno) {
    struct buf *b = bget(dev, blockno);
    if (!b->valid) {
        virtio_disk_rw(b, R);
        b->valid = 1;

        // 预读下一个块
        struct buf *next_b = bget(dev, blockno + 1);
        if (!next_b->valid) {
            // 异步读取下一个块
            async_read(next_b);
        }
        brelse(next_b);
    }
    return b;
}
```

**优点**：
- 顺序读取时提高性能
- 隐藏磁盘 I/O 延迟
- 充分利用磁盘带宽

**缺点**：
- 可能浪费 I/O（预读的块未使用）
- 增加复杂度
- 需要异步 I/O 支持

**2. 聚合写入**
```c
// 多个小写入合并为一个大写入
struct buf *b1 = bread(dev, blockno);
struct buf *b2 = bread(dev, blockno + 1);

// 修改两个块
modify(b1);
modify(b2);

// 一次性写入两个块（如果磁盘支持）
bwrite_multi(b1, b2);
```

**优点**：
- 减少磁盘寻道时间
- 提高吞吐量
- 充分利用磁盘带宽

**缺点**：
- 需要硬件支持
- 增加复杂度
- 延迟写入可能影响一致性

**3. 缓存优先级**
```c
struct buf {
    // ...
    int priority;  // 缓存优先级（1-10）
};
```

**策略**：
- 重要的块（超级块、inode）高优先级
- 数据块低优先级
- LRU 算法考虑优先级

---

### 11.6 调试与测试

#### 调试技巧

**1. 添加日志**
```c
struct buf *bread(uint dev, uint blockno) {
    printf("bread: dev=%d, blockno=%d\n", dev, blockno);
    struct buf *b = bget(dev, blockno);
    if (!b->valid) {
        printf("bread: cache miss, reading from disk\n");
        virtio_disk_rw(b, R);
        b->valid = 1;
    } else {
        printf("bread: cache hit\n");
    }
    return b;
}
```

**2. 统计信息**
```c
int bread_calls = 0;
int bread_hits = 0;
int bread_misses = 0;

struct buf *bread(uint dev, uint blockno) {
    bread_calls++;
    struct buf *b = bget(dev, blockno);
    if (!b->valid) {
        bread_misses++;
        virtio_disk_rw(b, R);
        b->valid = 1;
    } else {
        bread_hits++;
    }
    return b;
}

// 打印统计
void print_bread_stats() {
    printf("bread: calls=%d, hits=%d, misses=%d, hit_rate=%.2f%%\n",
           bread_calls, bread_hits, bread_misses,
           100.0 * bread_hits / bread_calls);
}
```

**3. 断言检查**
```c
struct buf *bread(uint dev, uint blockno) {
    struct buf *b = bget(dev, blockno);
    if (!b->valid) {
        virtio_disk_rw(b, R);
        b->valid = 1;
    }

    // 断言：缓冲区必须有效
    assert(b->valid == 1);
    assert(b->dev == dev);
    assert(b->blockno == blockno);
    assert(b->refcnt > 0);

    return b;
}
```

#### 测试用例

**测试 1：基本读取**
```c
void test_bread_basic() {
    struct buf *b = bread(ROOTDEV, 0);
    assert(b != NULL);
    assert(b->valid == 1);
    assert(b->dev == ROOTDEV);
    assert(b->blockno == 0);
    brelse(b);
    printf("test_bread_basic: OK\n");
}
```

**测试 2：缓存命中**
```c
void test_bread_cache_hit() {
    // 第一次读取：缓存未命中
    struct buf *b1 = bread(ROOTDEV, 0);
    assert(b1->valid == 1);
    brelse(b1);

    // 第二次读取：缓存命中
    struct buf *b2 = bread(ROOTDEV, 0);
    assert(b2->valid == 1);
    assert(b1 == b2);  // 应该是同一个缓冲区
    brelse(b2);

    printf("test_bread_cache_hit: OK\n");
}
```

**测试 3：并发读取**
```c
void test_bread_concurrent() {
    struct buf *b1 = bread(ROOTDEV, 0);
    struct buf *b2 = bread(ROOTDEV, 0);

    // 两个缓冲区应该相同
    assert(b1 == b2);
    assert(b1->refcnt == 2);

    brelse(b1);
    assert(b2->refcnt == 1);

    brelse(b2);
    assert(b2->refcnt == 0);

    printf("test_bread_concurrent: OK\n");
}
```

**测试 4：写入**
```c
void test_bwrite() {
    // 分配一个测试块
    uint bno = alloc_block(ROOTDEV);

    // 读取并修改
    struct buf *b = bread(ROOTDEV, bno);
    strcpy(b->data, "test data");
    bwrite(b);
    brelse(b);

    // 再次读取，验证数据
    b = bread(ROOTDEV, bno);
    assert(strcmp(b->data, "test data") == 0);
    brelse(b);

    // 释放块
    free_block(ROOTDEV, bno);

    printf("test_bwrite: OK\n");
}
```

---

### 11.7 总结

#### bread() 和 bwrite() 的核心作用

**bread()**：
- **接口**：文件系统与磁盘驱动的读接口
- **缓存**：透明的缓冲区缓存管理
- **同步**：同步阻塞读取，保证数据完整性
- **简洁**：简单的 API，易于使用

**bwrite()**：
- **持久化**：将内存数据写入磁盘
- **同步**：同步阻塞写入，保证数据一致性
- **简单**：直接调用底层驱动，无额外开销

#### 设计权衡

| 方面 | 简单实现 | 优化实现 |
|------|----------|----------|
| **写入策略** | 同步写入 | 延迟写（写回） |
| **预读** | 无 | 有 |
| **错误处理** | 简单或没有 | 完整的错误恢复 |
| **性能** | 中等 | 高 |
| **复杂度** | 低 | 高 |
| **适用场景** | 教学系统 | 生产系统 |

#### 关键要点

1. **缓存透明**：上层代码不需要关心缓存细节
2. **引用计数**：防止缓冲区在使用中被回收
3. **同步 I/O**：简化实现，保证一致性
4. **LRU 管理**：自动淘汰不常用的缓冲区
5. **错误处理**：教学系统可以简化，生产系统必须完善

理解 `bread()` 和 `bwrite()` 的工作原理，对于掌握文件系统的 I/O 机制至关重要。这两个函数是连接上层文件操作和底层磁盘驱动的桥梁，体现了操作系统设计中"分层"和"抽象"的核心思想。

---

## 12. Inode 管理与文件读写

### 12.1 背景与设计目标

#### 为什么需要 Inode 缓存？

在文件系统中，inode 是文件的"身份证"，包含了文件的所有元数据。每次访问文件都需要读取 inode，但频繁的磁盘 I/O 会严重影响性能。

**解决方案**：内存中的 inode 缓存（inode table）

**核心思想**：
1. **共享机制**：多个进程打开同一个文件时，共享同一个内存 inode
2. **延迟读取**：只在需要时才从磁盘读取 dinode
3. **引用计数**：防止正在使用的 inode 被回收
4. **写回策略**：修改后的 inode 在适当时机写回磁盘

#### 设计对比：缓冲区缓存 vs Inode 缓存

| 特性 | 缓冲区缓存 (buf) | Inode 缓存 (inode) |
|------|-----------------|-------------------|
| **缓存对象** | 磁盘块（固定大小） | 文件元数据（可变大小） |
| **缓存单位** | 块号 (dev, blockno) | inode 编号 (dev, inum) |
| **查找方式** | 哈希表或线性搜索 | 线性搜索（教学实现） |
| **生命周期** | 引用计数 + LRU | 引用计数 |
| **同步方式** | bread/bwrite | ivalid/iupdate |
| **共享机制** | 多个读者共享 | 多个进程共享 |

---

### 12.2 iget() - 获取 Inode 缓存

#### 完整代码

```c
// 找到 inum 号 dinode 绑定的 inode，如果不存在新绑定一个
static struct inode *iget(uint dev, uint inum) {
    struct inode *ip, *empty;

    // 遍历查找 inode table
    for (ip = &itable.inode[0]; ip < &itable.inode[NINODE]; ip++) {
        // 如果有对应的，引用计数 +1 并返回
        if (ip->ref > 0 && ip->dev == dev && ip->inum == inum) {
            ip->ref++;
            return ip;
        }
    }

    // 如果没有对应的，找一个空闲 inode 完成绑定
    empty = find_empty();

    // GG，inode 表满了，果断自杀。lab7 正常不会出现这个情况。
    if (empty == 0)
        panic("iget: no inodes");

    // 注意这里仅仅是写了元数据，没有实际读取，实际读取推迟到后面
    ip = empty;
    ip->dev = dev;
    ip->inum = inum;
    ip->ref = 1;
    ip->valid = 0;  // 没有实际读取，valid = 0
    return ip;
}
```

#### 详细说明

**作用**：获取指定设备的 inode 缓存。如果缓存中不存在，则分配一个新的 inode 并绑定到对应的磁盘 inode。

**参数**：
- `dev`：设备号
- `inum`：inode 编号（文件在磁盘上的唯一标识）

**返回值**：
- 指向内存 inode 的指针

#### 执行流程

**步骤 1：遍历查找**

```c
for (ip = &itable.inode[0]; ip < &itable.inode[NINODE]; ip++) {
    if (ip->ref > 0 && ip->dev == dev && ip->inum == inum) {
        ip->ref++;
        return ip;
    }
}
```

**itable 结构**：
```c
struct {
    struct inode inode[NINODE];  // 全局 inode 表
    // 可能还有锁、统计信息等
} itable;
```

**查找条件**：
1. `ip->ref > 0`：inode 正在被使用（未释放）
2. `ip->dev == dev`：设备号匹配
3. `ip->inum == inum`：inode 编号匹配

**找到后的操作**：
- `ip->ref++`：增加引用计数
- `return ip`：直接返回缓存的 inode

**为什么三个条件都要满足？**
- `ref > 0`：跳过空闲的 inode
- `dev == dev`：区分不同设备的文件系统
- `inum == inum`：精确定位到具体的文件

**步骤 2：查找空闲 inode**

```c
empty = find_empty();
```

**find_empty() 的实现**（推测）：
```c
struct inode *find_empty() {
    struct inode *ip;
    for (ip = &itable.inode[0]; ip < &itable.inode[NINODE]; ip++) {
        if (ip->ref == 0)  // 引用计数为 0 表示空闲
            return ip;
    }
    return 0;  // 没有找到空闲 inode
}
```

**为什么查找两次？**
- 第一次：查找已绑定的 inode（缓存命中）
- 第二次：查找空闲的 inode slot（缓存未命中）

**步骤 3：分配并初始化**

```c
if (empty == 0)
    panic("iget: no inodes");

ip = empty;
ip->dev = dev;
ip->inum = inum;
ip->ref = 1;
ip->valid = 0;  // 关键：标记为无效
return ip;
```

**初始化字段**：
- `dev`：设备号
- `inum`：inode 编号
- `ref = 1`：初始引用计数（调用者持有一个引用）
- `valid = 0`：**关键**：标记为无效，表示尚未从磁盘读取

**延迟读取策略**：
- `iget()` 只分配内存结构，**不读取磁盘**
- 实际的 dinode 读取推迟到 `ivalid()` 调用
- 优点：避免不必要的磁盘 I/O

#### 使用示例

```c
// 打开文件
struct inode *ip = iget(rootdev, inum);

// 确保 inode 有效（从磁盘读取）
ivalid(ip);

// 使用 inode
printf("文件大小: %d\n", ip->size);

// 释放 inode
iput(ip);  // ref--，如果 ref==0 则回收
```

#### 设计优点

**1. 共享机制**：
- 多个进程打开同一个文件时，`iget()` 返回同一个 inode
- 引用计数记录共享者数量
- 节省内存，保持一致性

**2. 延迟加载**：
- 只在需要时才读取磁盘
- 减少不必要的 I/O
- 提高性能

**3. 简单实现**：
- 线性搜索，易于理解
- 固定大小的数组，管理简单
- 适合教学和小规模系统

#### 性能优化方向

**当前实现的缺陷**：
- 线性搜索：O(N) 复杂度，N = NINODE（通常 50-200）
- 没有哈希表：无法快速定位
- 没有优先级：所有 inode 平等

**优化方案**：
```c
// 使用哈希表
#define NHASH 13
struct {
    struct inode inode[NINODE];
    struct inode *hash[NHASH];  // 哈希表头指针
} itable;

struct inode *iget(uint dev, uint inum) {
    uint hash = (dev + inum) % NHASH;
    struct inode *ip;

    // 在哈希链中查找
    for (ip = itable.hash[hash]; ip; ip = ip->hash_next) {
        if (ip->ref > 0 && ip->dev == dev && ip->inum == inum) {
            ip->ref++;
            return ip;
        }
    }

    // ... 分配新 inode 并加入哈希链
}
```

**优点**：
- 平均 O(1) 查找时间
- 减少缓存冲突
- 支持更多 inode

---

### 12.3 ivalid() - 使 Inode 有效

#### 完整代码

```c
// 如有必要则从磁盘读取 inode
void ivalid(struct inode *ip) {
    struct buf *bp;
    struct dinode *dip;

    if (ip->valid == 0) {
        // bread 可以完成一个块的读取
        // IBLOCK 可以计算 inum 在第几个 block
        bp = bread(ip->dev, IBLOCK(ip->inum, sb));

        // 得到 dinode 内容
        dip = (struct dinode *) bp->data + ip->inum % IPB;

        // 完成实际读取
        ip->type = dip->type;
        ip->size = dip->size;
        memmove(ip->addrs, dip->addrs, sizeof(ip->addrs));

        // buf 暂时没用了
        brelse(bp);

        // 现在有效了
        ip->valid = 1;
    }
}
```

#### 详细说明

**作用**：如果内存 inode 无效（`valid == 0`），则从磁盘读取对应的 dinode，填充内存 inode 的字段。

**参数**：
- `ip`：指向内存 inode 的指针

**何时调用**：
- 在使用 inode 的任何字段（`type`、`size`、`addrs`）之前
- 通常在 `iget()` 之后立即调用

#### 执行流程

**步骤 1：检查有效性**

```c
if (ip->valid == 0) {
    // 需要从磁盘读取
}
```

**如果已经有效**：
- 直接返回，避免重复读取
- 内存中的数据是最新的

**步骤 2：计算磁盘块号**

```c
bp = bread(ip->dev, IBLOCK(ip->inum, sb));
```

**IBLOCK 宏的定义**（推测）：
```c
#define IBLOCK(inum, sb) ((inum) / IPB + (sb)->inodestart)
```

**参数解释**：
- `inum`：inode 编号
- `sb`：超级块指针
- `IPB`：每个块的 inode 数量（IPB = BSIZE / sizeof(struct dinode)）

**计算过程**：
1. `inum / IPB`：inode 在 inode 区域内的块偏移
2. `+ (sb)->inodestart`：加上 inode 区域的起始块号
3. 得到：包含该 inode 的磁盘块号

**示例**：
- 假设 `BSIZE = 1024`，`sizeof(dinode) = 64`
- 则 `IPB = 1024 / 64 = 16`（每个块 16 个 inode）
- 如果 `inum = 20`，`inodestart = 32`
- 则 `IBLOCK(20, sb) = 20 / 16 + 32 = 1 + 32 = 33`

**步骤 3：定位 dinode**

```c
dip = (struct dinode *) bp->data + ip->inum % IPB;
```

**指针运算**：
1. `bp->data`：指向磁盘块的数据起始位置
2. `(struct dinode *) bp->data`：转换为 `dinode` 指针
3. `+ ip->inum % IPB`：指针偏移到具体的 dinode

**示例**（续）：
- `ip->inum % IPB = 20 % 16 = 4`
- `dip` 指向该块的第 4 个 dinode（从 0 开始）
- 该块包含 inum 16-31 的 dinode

**步骤 4：复制元数据**

```c
ip->type = dip->type;
ip->size = dip->size;
memmove(ip->addrs, dip->addrs, sizeof(ip->addrs));
```

**复制的字段**：
1. `type`：文件类型
2. `size`：文件大小
3. `addrs`：数据块地址数组

**为什么不用 `ip = *dip`？**
- 内存 inode 和磁盘 dinode 的结构不同
- 内存 inode 有额外的管理字段（`dev`、`inum`、`ref`、`valid`）
- 不能直接赋值，需要字段级别的复制

**步骤 5：清理和标记**

```c
brelse(bp);
ip->valid = 1;
```

**brelse(bp)**：
- 释放缓冲区（`refcnt--`）
- 缓冲区保留在缓存中，供后续使用

**ip->valid = 1**：
- 标记 inode 有效
- 后续的 `ivalid()` 调用会直接返回
- 表示内存中的数据是磁盘的最新副本

#### 与 iget() 的配合

```c
// 典型的使用流程
struct inode *ip = iget(dev, inum);  // 获取缓存（可能无效）
ivalid(ip);                          // 确保有效（如果需要则读取磁盘）

// 现在可以安全地使用 ip 的字段
if (ip->type == T_FILE) {
    printf("文件大小: %d\n", ip->size);
}

// 使用完后释放
iput(ip);
```

**为什么分离 iget() 和 ivalid()？**
- **性能优化**：不是所有操作都需要读取磁盘
- **灵活性**：调用者可以选择何时读取
- **延迟加载**：避免不必要的 I/O

#### 错误处理

**潜在问题**：
- `bread()` 可能失败（磁盘错误）
- `dip->type` 可能无效（文件系统损坏）

**改进方案**：
```c
void ivalid(struct inode *ip) {
    if (ip->valid == 0) {
        struct buf *bp = bread(ip->dev, IBLOCK(ip->inum, sb));
        struct dinode *dip = (struct dinode *) bp->data + ip->inum % IPB;

        // 检查 dinode 有效性
        if (dip->type == 0) {
            panic("ivalid: inode not allocated");
        }

        // 复制数据
        ip->type = dip->type;
        ip->size = dip->size;
        memmove(ip->addrs, dip->addrs, sizeof(ip->addrs));

        brelse(bp);
        ip->valid = 1;
    }
}
```

---

### 12.4 readi() - 读取文件内容

#### 完整代码

```c
// 从 ip 对应文件读取 [off, off+n) 这一段数据到 dst
int readi(struct inode *ip, char* dst, uint off, uint n) {
    uint tot, m;
    struct buf *bp;

    for (tot = 0; tot < n; tot += m, off += m, dst += m) {
        // bmap 完成 off 到 block num 的对应
        bp = bread(ip->dev, bmap(ip, off / BSIZE));

        // 一次最多读一个块，实际读取长度为 m
        m = MIN(n - tot, BSIZE - off % BSIZE);
        memmove(dst, (char*)bp->data + (off % BSIZE), m);
        brelse(bp);
    }
    return tot;
}
```

#### 详细说明

**作用**：从文件的指定偏移量开始读取数据到目标缓冲区。

**参数**：
- `ip`：指向文件 inode 的指针
- `dst`：目标缓冲区（接收数据）
- `off`：文件偏移量（起始位置）
- `n`：要读取的字节数

**返回值**：
- 实际读取的字节数（通常等于 n）

#### 执行流程

**步骤 1：循环读取**

```c
for (tot = 0; tot < n; tot += m, off += m, dst += m) {
    // 每次循环读取最多一个块
}
```

**为什么需要循环？**
- 文件可能跨越多个块
- 每次只能读取一个块
- 需要处理跨块读取的情况

**变量说明**：
- `tot`：已读取的总字节数
- `m`：本次循环读取的字节数
- `off`：当前文件偏移量（每次增加 m）
- `dst`：当前目标缓冲区位置（每次增加 m）

**步骤 2：获取数据块**

```c
bp = bread(ip->dev, bmap(ip, off / BSIZE));
```

**off / BSIZE**：
- 计算当前偏移量在第几个数据块
- 例如：`off = 2048, BSIZE = 1024` → 块号 = 2

**bmap() 函数**：
- 将文件的逻辑块号转换为物理块号
- 处理直接块和间接块
- 如果需要，分配新块（文件扩展）

**bread() 函数**：
- 读取物理块到缓冲区
- 自动处理缓存

**步骤 3：计算读取长度**

```c
m = MIN(n - tot, BSIZE - off % BSIZE);
```

**n - tot**：
- 剩余要读取的字节数
- 例如：`n = 2000, tot = 500` → 剩余 1500 字节

**BSIZE - off % BSIZE**：
- 当前块剩余的可读字节数
- `off % BSIZE`：当前偏移在块内的位置
- 例如：`off = 1500, BSIZE = 1024` → `off % BSIZE = 476` → 剩余 `1024 - 476 = 548` 字节

**MIN()**：
- 取两者中的较小值
- 确保不超过：
  - 剩余要读取的字节数
  - 当前块的剩余空间

**示例**：
- 要读取 2000 字节，从偏移 500 开始
- 第一次循环：读取 `MIN(2000, 1024 - 500) = MIN(2000, 524) = 524` 字节
- 第二次循环：读取 `MIN(1476, 1024) = 1024` 字节
- 第三次循环：读取 `MIN(452, 1024) = 452` 字节
- 总共：524 + 1024 + 452 = 2000 字节

**步骤 4：复制数据**

```c
memmove(dst, (char*)bp->data + (off % BSIZE), m);
```

**(char*)bp->data + (off % BSIZE)**：
- 计算源地址（块内偏移）
- 例如：`off = 1500` → 块内偏移 `1500 % 1024 = 476`

**memmove()**：
- 从缓冲区复制 m 字节到 dst
- 使用 `memmove` 而不是 `memcpy`，处理可能的内存重叠

**步骤 5：释放缓冲区**

```c
brelse(bp);
```

- 减少缓冲区引用计数
- 缓冲区保留在缓存中

#### 使用示例

**示例 1：读取整个文件**

```c
struct inode *ip = iget(dev, inum);
ivalid(ip);

// 分配缓冲区
char *buf = malloc(ip->size);

// 读取整个文件
int n = readi(ip, buf, 0, ip->size);
printf("读取了 %d 字节\n", n);

// 使用数据
printf("文件内容: %s\n", buf);

// 清理
free(buf);
iput(ip);
```

**示例 2：读取文件的一部分**

```c
// 读取文件的前 100 字节
char header[100];
int n = readi(ip, header, 0, 100);

// 读取文件的最后 100 字节
char footer[100];
int offset = ip->size - 100;
if (offset < 0) offset = 0;
n = readi(ip, footer, offset, 100);
```

---

### 12.5 writei() - 写入文件内容

#### 完整代码

```c
// 同 readi
int writei(struct inode *ip, char* src, uint off, uint n) {
    uint tot, m;
    struct buf *bp;

    for (tot = 0; tot < n; tot += m, off += m, src += m) {
        bp = bread(ip->dev, bmap(ip, off / BSIZE));
        m = MIN(n - tot, BSIZE - off % BSIZE);
        memmove(src, (char*)bp->data + (off % BSIZE), m);
        bwrite(bp);  // 注意：写入磁盘
        brelse(bp);
    }

    // 文件长度变长，需要更新 inode 里的 size 字段
    if (off > ip->size)
        ip->size = off;

    // 有可能 inode 信息被更新了，写回
    iupdate(ip);

    return tot;
}
```

#### 详细说明

**作用**：将数据写入文件的指定偏移量。

**参数**：
- `ip`：指向文件 inode 的指针
- `src`：源缓冲区（包含要写入的数据）
- `off`：文件偏移量（起始位置）
- `n`：要写入的字节数

**返回值**：
- 实际写入的字节数（通常等于 n）

#### 与 readi() 的对比

| 操作 | readi() | writei() |
|------|---------|----------|
| **数据方向** | 磁盘 → 内存 | 内存 → 磁盘 |
| **缓冲区操作** | 只读 | 修改后写入 |
| **bread/bwrite** | 只用 bread | bread + bwrite |
| **文件大小** | 不变 | 可能增大 |
| **inode 更新** | 不需要 | 需要（iupdate） |

#### 执行流程

**步骤 1-4：循环写入**

```c
for (tot = 0; tot < n; tot += m, off += m, src += m) {
    bp = bread(ip->dev, bmap(ip, off / BSIZE));
    m = MIN(n - tot, BSIZE - off % BSIZE);
    memmove(src, (char*)bp->data + (off % BSIZE), m);
    bwrite(bp);  // 关键：写入磁盘
    brelse(bp);
}
```

**与 readi() 的区别**：
1. `src` 而不是 `dst`：源而不是目标
2. `bwrite(bp)` 而不是只用 `brelse(bp)`：持久化修改

**为什么每次都 bwrite？**
- 保证数据立即写入磁盘
- 避免数据丢失
- 简化实现（同步写入）

**步骤 5：更新文件大小**

```c
if (off > ip->size)
    ip->size = off;
```

**为什么需要更新？**
- 写入可能超出文件末尾
- 文件大小需要反映最新的长度

**示例**：
- 原文件大小：100 字节
- 在偏移 200 处写入 50 字节
- 新文件大小：250 字节（200 + 50）

**步骤 6：更新 inode**

```c
iupdate(ip);
```

**iupdate() 的作用**：
- 将修改后的内存 inode 写回磁盘
- 确保文件元数据持久化
- 防止掉电丢失

#### 使用示例

**示例 1：追加数据**

```c
struct inode *ip = iget(dev, inum);
ivalid(ip);

// 在文件末尾追加数据
char *data = "Hello, World!\n";
int n = writei(ip, data, ip->size, strlen(data));

printf("追加了 %d 字节，新大小: %d\n", n, ip->size);

iput(ip);
```

**示例 2：覆盖写入**

```c
// 从文件开头写入
char *data = "New content";
int n = writei(ip, data, 0, strlen(data));

printf("写入了 %d 字节\n", n);
```

---

### 12.6 bmap() - 块映射与文件扩展

#### 完整代码

```c
// bn = off / BSIZE
uint bmap(struct inode *ip, uint bn) {
    uint addr, *a;
    struct buf *bp;

    // 如果 bn < 12，属于直接索引，block num = ip->addrs[bn]
    if (bn < NDIRECT) {
        // 如果对应的 addr，也就是 block num = 0，表明文件大小增加，需要给文件分配新的 data block
        if ((addr = ip->addrs[bn]) == 0)
            ip->addrs[bn] = addr = balloc(ip->dev);
        return addr;
    }

    bn -= NDIRECT;

    // 间接索引块，那么对应的数据块就是一个大 addr 数组
    if (bn < NINDIRECT) {
        // Load indirect block, allocating if necessary.
        if ((addr = ip->addrs[NDIRECT]) == 0)
            ip->addrs[NDIRECT] = addr = balloc(ip->dev);

        bp = bread(ip->dev, addr);
        a = (uint *) bp->data;

        if ((addr = a[bn]) == 0) {
            a[bn] = addr = balloc(ip->dev);
            bwrite(bp);
        }

        brelse(bp);
        return addr;
    }

    panic("bmap: out of range");
    return 0;
}
```

#### 详细说明

**作用**：将文件的逻辑块号转换为物理块号。如果块不存在，则分配新块（文件扩展）。

**参数**：
- `ip`：指向文件 inode 的指针
- `bn`：逻辑块号（`off / BSIZE`）

**返回值**：
- 物理块号（磁盘上的实际块号）

#### 文件结构回顾

**直接块与间接块**：
```
inode.addrs[0]    → 直接块 0
inode.addrs[1]    → 直接块 1
...
inode.addrs[11]   → 直接块 11 (NDIRECT-1)
inode.addrs[12]   → 间接块 → NINDIRECT 个间接数据块
```

**参数定义**（推测）：
- `NDIRECT = 12`：直接块数量
- `NINDIRECT = BSIZE / 4`：间接块数量（每个块号 4 字节）
- 如果 `BSIZE = 1024`，则 `NINDIRECT = 256`

**最大文件大小**：
- 直接块：`12 × 1024 = 12 KB`
- 间接块：`256 × 1024 = 256 KB`
- 总计：`268 KB`

#### 执行流程

**情况 1：直接块**

```c
if (bn < NDIRECT) {
    if ((addr = ip->addrs[bn]) == 0)
        ip->addrs[bn] = addr = balloc(ip->dev);
    return addr;
}
```

**检查是否已分配**：
- `ip->addrs[bn] == 0`：块未分配（空洞或新文件）
- `ip->addrs[bn] != 0`：块已分配，直接返回

**分配新块**：
- `balloc(ip->dev)`：在位图中查找空闲块
- 更新 `ip->addrs[bn]`：记录新块号
- 注意：此时 inode 尚未写回磁盘（由 `iupdate()` 处理）

**情况 2：间接块**

```c
bn -= NDIRECT;  // 调整间接块索引

if (bn < NINDIRECT) {
    // 1. 确保间接块存在
    if ((addr = ip->addrs[NDIRECT]) == 0)
        ip->addrs[NDIRECT] = addr = balloc(ip->dev);

    // 2. 读取间接块
    bp = bread(ip->dev, addr);
    a = (uint *) bp->data;

    // 3. 查找或分配间接数据块
    if ((addr = a[bn]) == 0) {
        a[bn] = addr = balloc(ip->dev);
        bwrite(bp);  // 写回间接块
    }

    brelse(bp);
    return addr;
}
```

**步骤 1：调整索引**
- `bn -= NDIRECT`：将逻辑块号转换为间接块内的索引
- 例如：逻辑块号 15 → 间接块索引 3（15 - 12）

**步骤 2：确保间接块存在**
- 间接块本身也需要分配
- `ip->addrs[NDIRECT]`：指向间接块的物理块号

**步骤 3：读取间接块**
- 间接块是一个包含块号数组的数据块
- `(uint *) bp->data`：转换为 `uint` 数组

**步骤 4：查找或分配间接数据块**
- `a[bn]`：间接块中的第 bn 个条目
- 如果为 0，分配新数据块并更新间接块
- `bwrite(bp)`：将更新后的间接块写回磁盘

**情况 3：超出范围**

```c
panic("bmap: out of range");
```

- 文件太大，超出了文件系统的支持范围
- 通常是程序错误或文件系统损坏

#### 使用示例

**示例 1：小文件访问**

```c
// 文件大小：5 KB，需要 5 个块
uint off = 3000;  // 偏移量
uint bn = off / BSIZE;  // bn = 3000 / 1024 = 2

uint bno = bmap(ip, bn);
// bn < NDIRECT (12)，使用直接块
// 返回 ip->addrs[2]
```

**示例 2：大文件访问**

```c
// 文件大小：100 KB，需要 100 个块
uint off = 50000;  // 偏移量
uint bn = off / BSIZE;  // bn = 50000 / 1024 = 48

uint bno = bmap(ip, bn);
// bn >= NDIRECT (12)，使用间接块
// bn -= 12 → bn = 36
// 读取间接块，返回 a[36]
```

**示例 3：文件扩展**

```c
// 文件当前大小：10 KB
// 写入到偏移 20000，扩展到 20 KB
uint off = 20000;
uint bn = off / BSIZE;  // bn = 19

uint bno = bmap(ip, bn);
// 需要分配块 8-19（如果尚未分配）
// bmap 会自动调用 balloc 分配新块
```

---

### 12.7 iupdate() - 更新磁盘 Inode

#### 完整代码

```c
// 将修改后的内存 inode 复制到磁盘
// 必须在修改任何存在于磁盘上的 ip->xxx 字段后调用
void iupdate(struct inode *ip) {
    struct buf *bp;
    struct dinode *dip;

    bp = bread(ip->dev, IBLOCK(ip->inum, sb));
    dip = (struct dinode *) bp->data + ip->inum % IPB;

    // 复制修改后的字段
    dip->type = ip->type;
    dip->size = ip->size;
    memmove(dip->addrs, ip->addrs, sizeof(ip->addrs));

    bwrite(bp);
    brelse(bp);
}
```

#### 详细说明

**作用**：将修改后的内存 inode 写回磁盘，确保持久化。

**参数**：
- `ip`：指向内存 inode 的指针

**何时调用**：
- 修改了 `ip->type`（罕见）
- 修改了 `ip->size`（常见）
- 修改了 `ip->addrs`（文件扩展时）

#### 与 ivalid() 的对称性

| 操作 | ivalid() | iupdate() |
|------|----------|-----------|
| **方向** | 磁盘 → 内存 | 内存 → 磁盘 |
| **触发** | `valid == 0` | 修改字段后 |
| **目的** | 加载元数据 | 持久化修改 |
| **时机** | 延迟加载 | 显式调用 |

#### 执行流程

**步骤 1：读取磁盘块**

```c
bp = bread(ip->dev, IBLOCK(ip->inum, sb));
```

- 读取包含该 dinode 的磁盘块
- 使用缓冲区缓存

**步骤 2：定位 dinode**

```c
dip = (struct dinode *) bp->data + ip->inum % IPB;
```

- 在块内定位到具体的 dinode

**步骤 3：复制修改**

```c
dip->type = ip->type;
dip->size = ip->size;
memmove(dip->addrs, ip->addrs, sizeof(ip->addrs));
```

- 更新磁盘上的 dinode 字段
- 与 `ivalid()` 的复制方向相反

**步骤 4：写回磁盘**

```c
bwrite(bp);
brelse(bp);
```

- 将缓冲区写入磁盘
- 释放缓冲区

#### 使用示例

```c
// 修改文件大小
ip->size += 1024;
iupdate(ip);  // 立即写回

// 分配新块
uint bno = balloc(ip->dev);
ip->addrs[5] = bno;
iupdate(ip);  // 持久化
```

---

### 12.8 完整的文件读写流程

#### 读取文件：完整流程

```c
// 1. 获取 inode
struct inode *ip = iget(dev, inum);

// 2. 确保 inode 有效
ivalid(ip);

// 3. 读取文件内容
char *buf = malloc(size);
int n = readi(ip, buf, 0, size);

// 4. 使用数据
printf("文件内容: %s\n", buf);

// 5. 清理
free(buf);
iput(ip);
```

#### 写入文件：完整流程

```c
// 1. 获取 inode
struct inode *ip = iget(dev, inum);

// 2. 确保 inode 有效
ivalid(ip);

// 3. 写入文件内容
char *data = "Hello, World!";
int n = writei(ip, data, 0, strlen(data));

// writei 内部会：
// - 调用 bmap() 分配块（如果需要）
// - 调用 bread() 读取块
// - 修改块内容
// - 调用 bwrite() 写回块
// - 更新 ip->size
// - 调用 iupdate(ip) 写回 inode

// 4. 清理
iput(ip);
```

---

### 12.9 总结

#### 核心函数一览

| 函数 | 作用 | 层级 |
|------|------|------|
| **iget()** | 获取 inode 缓存 | 高层 |
| **ivalid()** | 确保 inode 有效 | 高层 |
| **iupdate()** | 更新磁盘 inode | 高层 |
| **readi()** | 读取文件内容 | 高层 |
| **writei()** | 写入文件内容 | 高层 |
| **bmap()** | 块映射与扩展 | 中层 |
| **bread()** | 读取磁盘块 | 底层 |
| **bwrite()** | 写入磁盘块 | 底层 |
| **balloc()** | 分配新块 | 底层 |

#### 设计亮点

1. **分层抽象**：
   - 高层函数调用中层函数
   - 中层函数调用底层函数
   - 每层只关注自己的职责

2. **延迟策略**：
   - `iget()` 不立即读取磁盘
   - `ivalid()` 按需加载
   - 减少不必要的 I/O

3. **缓存机制**：
   - inode 缓存共享
   - 缓冲区缓存透明
   - 多层缓存提高性能

4. **原子性**：
   - `iupdate()` 确保元数据持久化
   - `bwrite()` 确保数据持久化
   - 避免数据不一致

5. **扩展性**：
   - `bmap()` 自动处理文件扩展
   - 支持直接块和间接块
   - 支持文件空洞

理解这些函数的工作原理和相互关系，是掌握文件系统实现的关键。下一章我们将学习更高层的文件操作接口。

---

## 13. 文件描述符管理

### 13.1 fileclose() - 关闭文件描述符

#### 完整代码

```c
void fileclose(struct file *f) {
    if (f->ref < 1)
        panic("fileclose");

    // 减少引用计数，如果还有其他引用则直接返回
    if (--f->ref > 0) {
        return;
    }

    // 根据文件类型执行相应的清理操作
    switch (f->type) {
    case FD_STDIO:
        // 标准输入输出（stdin/stdout/stderr），无需特殊处理
        break;

    case FD_INODE:
        // 普通文件：释放 inode 引用（ref--）
        iput(f->ip);
        break;

    default:
        panic("unknown file type %d\n", f->type);
    }

    // 清空文件描述符的所有字段，标记为未使用
    f->off = 0;         // 重置文件偏移量
    f->readable = 0;    // 清除可读标志
    f->writable = 0;    // 清除可写标志
    f->ref = 0;         // 重置引用计数
    f->type = FD_NONE;  // 标记为未使用类型
}
```

#### 作用

**关闭文件描述符**，释放相关资源。支持引用计数机制，允许多个地方共享同一个文件描述符。

#### 关键注释

- **引用计数检查**：`ref < 1` 表示异常情况（已关闭或未初始化）
- **引用计数递减**：`--f->ref`，如果仍大于 0 说明有其他地方在使用
- **类型相关清理**：
  - `FD_STDIO`：标准输入输出，无需操作
  - `FD_INODE`：释放 inode 引用（`iput()`）
- **清空字段**：重置所有字段，避免悬垂指针

---

### 13.2 fdalloc() - 分配文件描述符

#### 相关结构

```c
// proc.h
// 每个进程的状态
struct proc {
    // ...

    struct file* files[16];  // 文件描述符表
};
```

#### 完整代码

```c
// os/proc.c
int fdalloc(struct file* f) {
    struct proc* p = curr_proc();

    // fd = 0,1,2 预留给 stdio/stdout/stderr
    for(int i = 3; i < FD_MAX; ++i) {
        if(p->files[i] == 0) {
            p->files[i] = f;
            return i;  // 返回分配的文件描述符
        }
    }
    return -1;  // 文件描述符表已满
}
```

#### 作用

**为当前进程分配一个文件描述符**，将文件结构指针 `f` 关联到进程的文件描述符表中，返回分配的文件描述符编号。

#### 关键注释

- **保留描述符**：0、1、2 分别预留给 stdin、stdout、stderr
- **查找空闲槽位**：从描述符 3 开始遍历，找到第一个空闲位置（`p->files[i] == 0`）
- **关联文件结构**：将文件结构指针存入文件描述符表
- **返回值**：成功返回文件描述符编号（≥3），失败返回 -1（表满）

---

### 13.3 iput() - 释放 Inode 引用

#### 完整代码

```c
void iput(struct inode *ip) {
    ip->ref--;
}
```

#### 作用

**减少 inode 的引用计数**。当引用计数降为 0 时，inode 可以被回收。与 `iget()` 相反，`iget()` 增加引用计数，`iput()` 减少引用计数。

#### 关键注释

- **引用计数递减**：`ip->ref--`，减少一个引用
- **简化实现**：当前版本只递减计数，不检查是否为 0
- **与 iget 配对**：每次 `iget()` 都应对应一次 `iput()`
- **资源管理**：完整实现应该在 `ref == 0` 时回收 inode（写回磁盘、标记为空闲等）

---

## 14. 路径解析与目录查找

### 14.1 namei() - 路径名解析

#### 完整代码

```c
// namei = 获得根目录，然后在其中遍历查找 path
struct inode *namei(char *path) {
    struct inode *dp = root_dir();
    return dirlookup(dp, path, 0);
}
```

#### 作用

**将路径名解析为对应的 inode**。从根目录开始，查找路径对应的文件或目录，返回其 inode。

#### 关键注释

- **获取根目录**：调用 `root_dir()` 获取根目录的 inode
- **目录查找**：调用 `dirlookup()` 在目录中查找路径名
- **简化实现**：当前版本只支持根目录下的文件查找

---

### 14.2 root_dir() - 获取根目录

#### 完整代码

```c
// root_dir 位置固定
struct inode *root_dir() {
    struct inode* r = iget(ROOTDEV, ROOTINO);
    ivalid(r);
    return r;
}
```

#### 作用

**获取文件系统根目录的 inode**。根目录的位置是固定的（设备 `ROOTDEV`，inode 号 `ROOTINO`）。

#### 关键注释

- **固定位置**：根目录的设备号和 inode 号是常量
- **获取缓存**：调用 `iget()` 获取根目录的 inode 缓存
- **确保有效**：调用 `ivalid()` 确保inode从磁盘加载

---

### 14.3 dirlookup() - 目录项查找

#### 完整代码

```c
// 遍历根目录所有的 dirent，找到 name 一样的 inode
struct inode *dirlookup(struct inode *dp, char *name, uint *poff) {
    uint off, inum;
    struct dirent de;

    // 每次迭代处理一个 block，注意根目录可能有多个 data block
    for (off = 0; off < dp->size; off += sizeof(de)) {
        readi(dp, 0, (uint64) &de, off, sizeof(de));

        if (strncmp(name, de.name, DIRSIZ) == 0) {
            if (poff)
                *poff = off;
            inum = de.inum;

            // 找到之后，绑定一个内存 inode 然后返回
            return iget(dp->dev, inum);
        }
    }

    return 0;  // 未找到
}
```

#### 作用

**在目录中查找指定名称的目录项**，返回对应文件的 inode。遍历目录的所有数据块，比较每个目录项的文件名。

#### 关键注释

- **遍历目录数据**：使用 `readi()` 读取目录的内容（目录项数组）
- **跨块处理**：目录可能包含多个数据块，循环遍历所有块
- **名称比较**：使用 `strncmp()` 比较文件名（最多 `DIRSIZ` 个字符）
- **返回偏移**：可选参数 `poff` 用于返回找到的目录项在目录中的偏移量
- **返回 inode**：找到后通过 `iget()` 获取文件对应的 inode
- **未找到**：返回 0（NULL）

---

## 15. 文件与目录创建

### 15.1 create() - 创建文件

#### 完整代码

```c
static struct inode *create(char *path, short type) {
    struct inode *ip, *dp;

    if((ip = namei(path)) != 0) {
        // 已经存在，直接返回
        return ip;
    }

    // 创建一个文件，首先分配一个空闲的 disk inode，绑定内存 inode 之后返回
    ip = ialloc(dp->dev, type);

    // 注意 ialloc 不会执行实际读取，必须有 ivalid
    ivalid(ip);

    // 在根目录创建一个 dirent 指向刚才创建的 inode
    dirlink(dp, path, ip->inum);

    // dp 不用了，iput 就是释放内存 inode，和 iget 正好相反
    iput(dp);

    return ip;
}
```

#### 作用

**创建一个新文件或目录**。首先检查文件是否已存在，如果不存在则分配新的 inode，并在目录中添加目录项。

#### 关键注释

- **检查存在性**：调用 `namei()` 查找文件，已存在则直接返回
- **分配 inode**：调用 `ialloc()` 分配一个新的磁盘 inode
- **确保有效**：调用 `ivalid()` 从磁盘加载 inode（初始化）
- **添加目录项**：调用 `dirlink()` 在目录中创建文件名到 inode 的映射
- **释放目录**：调用 `iput()` 释放目录 inode 的引用

---

### 15.2 ialloc() - 分配磁盘 Inode

#### 完整代码

```c
// nfs/fs.c
uint ialloc(ushort type) {
    uint inum = freeinode++;
    struct dinode din;

    bzero(&din, sizeof(din));
    din.type = xshort(type);
    din.size = xint(0);
    winode(inum, &din);

    return inum;
}
```

#### 作用

**分配一个新的磁盘 inode**。从空闲 inode 池中取出一个 inode 号，初始化其元数据并写入磁盘。

#### 关键注释

- **分配编号**：从全局变量 `freeinode` 获取下一个空闲 inode 号
- **清零结构**：使用 `bzero()` 清空 dinode 结构
- **设置类型**：`xshort()` 转换字节序，设置文件类型
- **初始化大小**：`xint()` 转换字节序，初始大小为 0
- **写回磁盘**：调用 `winode()` 将 dinode 写入磁盘
- **返回编号**：返回分配的 inode 号

---

### 15.3 dirlink() - 添加目录项

#### 完整代码

```c
// os/fs.c
// 在目录 dp 中写入一个新的目录项 (name, inum)
int dirlink(struct inode *dp, char *name, uint inum) {
    int off;
    struct dirent de;
    struct inode *ip;

    // 检查名称是否已存在
    if((ip = dirlookup(dp, name, 0)) != 0){
        iput(ip);
        return -1;
    }

    // 查找一个空的目录项
    for(off = 0; off < dp->size; off += sizeof(de)){
        if(readi(dp, 0, (uint64)&de, off, sizeof(de)) != sizeof(de))
            panic("dirlink read");

        if(de.inum == 0)
            break;
    }

    strncpy(de.name, name, DIRSIZ);
    de.inum = inum;

    if(writei(dp, 0, (uint64)&de, off, sizeof(de)) != sizeof(de))
        panic("dirlink");

    return 0;
}
```

#### 作用

**在目录中添加一个新的目录项**（文件名到 inode 的映射）。首先检查名称是否冲突，然后找到空闲位置或追加到目录末尾。

#### 关键注释

- **检查冲突**：调用 `dirlookup()` 检查文件名是否已存在
- **查找空位**：遍历目录项，找到 `inum == 0` 的空闲槽位
- **填充目录项**：使用 `strncpy()` 复制文件名，设置 inode 号
- **写回目录**：调用 `writei()` 将目录项写入目录文件
- **错误处理**：读取或写入失败时触发 panic

---

**下一章预告**：我们将学习文件系统调用的完整实现，包括 open、read、write 等系统调用。

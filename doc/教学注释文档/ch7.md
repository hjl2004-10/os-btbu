# lab7

## 第七章：进程间通信 - 管道

## 概述

本章介绍 uCore 操作系统中的进程间通信（IPC）机制，重点讲解管道（Pipe）的实现。管道是 Unix/Linux 系统中最基础的 IPC 机制，允许一个进程的输出直接作为另一个进程的输入，实现了进程间的数据流动。

---

## 1. 管道的基本概念

### 1.1 什么是管道？

**管道**（Pipe）是一种进程间通信机制，允许一个进程（写者）将数据写入管道，另一个进程（读者）从管道中读取数据。

**核心特点**：
- **单向通信**：数据只能在一个方向上流动
- **字节流**：管道传输的是无结构的字节流
- **缓冲机制**：管道有固定大小的缓冲区
- **原子性**：小于 PIPE_BUF 大小的写入是原子的

**典型应用场景**：
- Shell 管道：`cmd1 | cmd2`
- 进程间数据传递
- 父子进程通信

### 1.2 管道的工作原理

```
写进程          管道缓冲区          读进程
   │                              │
   ├────写入数据──→  [数据队列] ────┼──读取数据────→
   │                              │
```

**关键概念**：
- **写端**：只能写入数据的文件描述符
- **读端**：只能读取数据的文件描述符
- **共享缓冲区**：读写两端共享同一个内核缓冲区
- **阻塞行为**：
  - 读空管道时阻塞（等待数据）
  - 写满管道时阻塞（等待空间）

---

## 2. pipealloc() - 创建管道

### 2.1 完整代码

```c
int pipealloc(struct file *f0, struct file *f1)
{
    // 这里没有用预分配，由于 pipe 比较大，直接拿一个页过来，也不算太浪费
    struct pipe *pi = (struct pipe*)kalloc();

    // 一开始 pipe 可读可写，但是已读和已写内容为 0
    pi->readopen = 1;
    pi->writeopen = 1;
    pi->nwrite = 0;
    pi->nread = 0;

    // 两个参数分别通过 filealloc 得到，把该 pipe 和这两个文件关连
    // 一端可读，一端可写。读写端控制是 sys_pipe 的要求。
    f0->type = FD_PIPE;
    f0->readable = 1;
    f0->writable = 0;
    f0->pipe = pi;

    f1->type = FD_PIPE;
    f1->readable = 0;
    f1->writable = 1;
    f1->pipe = pi;

    return 0;
}
```

### 2.2 详细说明

**作用**：创建一个管道，并返回两个文件描述符，一个用于读取（f0），一个用于写入（f1）。

**参数**：
- `f0`：指向读端文件结构的指针
- `f1`：指向写端文件结构的指针

**返回值**：
- 成功返回 0
- 失败返回负值（如内存不足）

### 2.3 执行流程

#### 步骤 1：分配管道结构

```c
struct pipe *pi = (struct pipe*)kalloc();
```

**kalloc() 的作用**：
- 从内核内存分配器获取一页内存（通常 4096 字节）
- 用于存储管道结构及其缓冲区

**为什么直接分配一页？**
- 管道结构较大（包含缓冲区）
- 预分配太浪费（可能用不到）
- 一页内存（4KB）对于管道缓冲区来说大小合适
- 简化内存管理

#### 步骤 2：初始化管道状态

```c
pi->readopen = 1;    // 读端开放
pi->writeopen = 1;   // 写端开放
pi->nwrite = 0;      // 已写字节数
pi->nread = 0;       // 已读字节数
```

**字段说明**：

- **readopen**：
  - `1`：读端仍然开放
  - `0`：读端已关闭
  - 当读端关闭时，写进程向管道写入会收到 SIGPIPE 信号

- **writeopen**：
  - `1`：写端仍然开放
  - `0`：写端已关闭
  - 当写端关闭时，读进程读取管道会返回 EOF（文件结束）

- **nwrite**：
  - 累计写入管道的字节数
  - 用于计算当前管道中的数据量
  - 帮助判断管道是否为空或已满

- **nread**：
  - 累计从管道读取的字节数
  - 与 nwrite 配合使用
  - 当前管道中的数据量 = nwrite - nread

#### 步骤 3-4：配置读写端

```c
// 配置读端 f0
f0->type = FD_PIPE;
f0->readable = 1;
f0->writable = 0;
f0->pipe = pi;

// 配置写端 f1
f1->type = FD_PIPE;
f1->readable = 0;
f1->writable = 1;
f1->pipe = pi;
```

**关键点**：
- `f0->pipe = pi` 和 `f1->pipe = pi` 指向同一个管道结构
- 通过 readable/writable 标志控制读写权限
- 保证数据的单向流动

### 2.4 关键设计点

#### 1. 共享管道结构

```c
f0->pipe = pi;  // 读端
f1->pipe = pi;  // 写端（同一个 pi！）
```

**为什么两个文件描述符指向同一个管道？**
- 管道是双向共享的通信通道
- 写入 f1 的数据可以从 f0 读取
- 通过共享的 `struct pipe` 实现数据传递

**类比**：
- 就像水管的两端，连接同一个管道
- 从一端注入水，从另一端取出水

#### 2. 单向数据流

```
f1 (写端) ─────写入────→ [管道缓冲区] ────读取────→ f0 (读端)
```

**控制机制**：
- 通过 `readable` 和 `writable` 标志控制读写权限
- 防止进程从写端读取或向读端写入
- 保证数据的单向流动

#### 3. 引用计数管理

**管道结构的生命周期**：
- 初始状态：两个文件描述符都引用管道
- 当读端关闭：`pi->readopen = 0`
- 当写端关闭：`pi->writeopen = 0`
- 当两端都关闭：管道可以释放

---

## 3. 进程退出时的资源清理

### 3.1 freeproc() - 清理进程资源

#### 完整代码

```c
void freeproc(struct proc *p)
{
    // ... 其他清理代码 ...

    // 清理文件描述符表
    for (int i = 3; i < FD_BUFFER_SIZE; i++) {
        if (p->files[i] != NULL) {
            fileclose(p->files[i]);
        }
    }

    // ... 其他清理代码 ...
}
```

#### 详细说明

**作用**：在进程退出时，清理进程打开的所有文件描述符，释放相关资源。

**执行时机**：
- 进程调用 exit() 主动退出
- 进程被信号杀死
- 父进程回收子进程

**清理范围**：
- 从描述符 3 开始（跳过 stdin、stdout、stderr）
- 遍历到 `FD_BUFFER_SIZE`（文件描述符表的大小）
- 关闭所有已打开的文件描述符

#### 关键注释

**为什么从描述符 3 开始？**
```c
for (int i = 3; i < FD_BUFFER_SIZE; i++)
```
- 描述符 0、1、2 分别预留给 stdin、stdout、stderr
- 这些标准 I/O 描述符由系统统一管理
- 进程退出时不需要（也不应该）关闭它们

**为什么需要清理？**

1. **释放 inode 引用**：
   - 每个打开的文件都持有 inode 的引用
   - `fileclose()` 会调用 `iput()` 减少引用计数
   - 不清理会导致 inode 无法被回收

2. **释放管道资源**：
   - 如果文件描述符指向管道
   - 关闭读端或写端会影响管道的状态
   - 两端都关闭时，管道缓冲区会被释放

3. **释放文件结构**：
   - 每个文件描述符对应一个 `struct file`
   - 清理后这些结构可以被重用

**对管道的影响**：

假设进程持有管道的两端：
```c
p->files[4] = 管道读端
p->files[5] = 管道写端
```

执行 `fileclose()` 后：
```c
// 关闭读端
pi->readopen = 0;

// 关闭写端
pi->writeopen = 0;

// 两端都关闭，释放管道
if (pi->readopen == 0 && pi->writeopen == 0) {
    kfree(pi);  // 释放管道内存
}
```

#### 设计考虑

**1. 为什么不关闭标准 I/O？**
- 标准输入输出可能由多个进程共享
- 系统负责在进程终止时自动处理
- 避免影响其他进程的标准 I/O

**2. 清理顺序的重要性**：
- 从低描述符到高描述符（3 → FD_BUFFER_SIZE）
- 顺序不重要，因为每个描述符独立

**3. 内存泄漏预防**：
- 如果不清理，会导致资源泄漏：
  - inode 引用计数不归零
  - 管道缓冲区无法释放
  - 文件结构无法重用

---

## 4. 总结

### 4.1 pipealloc() 的核心作用

1. **分配资源**：为管道分配内存和缓冲区
2. **初始化状态**：设置管道的读写状态和计数器
3. **配置文件描述符**：创建读写两个文件描述符
4. **建立关联**：将文件描述符与管道结构关联
5. **控制权限**：通过 readable/writable 标志控制读写方向

### 4.2 设计亮点

1. **简洁高效**：
   - 直接分配一页内存，避免复杂的预分配机制
   - 通过共享指针实现双向通信

2. **单向保证**：
   - 使用 readable/writable 标志强制单向数据流
   - 防止误用（从写端读取、向读端写入）

3. **资源管理**：
   - 通过引用计数（readopen/writeopen）管理生命周期
   - 两端都关闭时自动释放资源
   - 进程退出时自动清理所有打开的文件描述符

4. **阻塞语义**：
   - 读空等待、写满等待，符合 POSIX 标准
   - 根据对端状态决定阻塞或返回

理解 `pipealloc()` 的工作原理，是掌握管道实现的关键。管道虽然简单，却是 Unix 哲学"一切皆文件"和"组合小工具完成大任务"的完美体现。

---

## 5. 管道通信的实际应用

### 5.1 fork() 和 exec() 对文件描述符的影响

#### 关键特性

**fork() 之后**：
- 子进程继承父进程的所有文件描述符
- 文件描述符表被完整复制
- 文件偏移量也在父子进程间共享

**exec() 之后**：
- **默认不会关闭已打开的文件描述符**
- 除非设置了 FD_CLOEXEC 标志
- 这是管道通信能够工作的基础

#### 为什么 exec 不关闭文件描述符？

```c
int fd[2];
pipe(fd);

if (fork() == 0) {
    // 子进程
    exec(cmd, args);  // fd[0] 和 fd[1] 仍然有效！
}
```

**原因**：
1. **灵活性**：允许程序在 exec 前打开文件，exec 后继续使用
2. **管道通信**：父进程可以通过管道与 exec 后的子进程通信
3. **重定向**：支持 shell 的 I/O 重定向功能

### 5.2 完整示例：父子进程管道通信

#### 用户程序代码

```c
// user/src/ch6b_pipetest.c

char STR[] = "hello pipe!";

int main() {
    uint64 pipe_fd[2];
    int ret = pipe(&pipe_fd);

    if (fork() == 0) {
        // 子进程：从管道读取数据，并与 STR 比较
        char buffer[32 + 1];
        read(pipe_fd[0], buffer, 32);
        assert(strncmp(buffer, STR, strlen(STR)) == 0);
        exit(0);
    } else {
        // 父进程：向管道写入数据
        write(pipe_fd[1], STR, strlen(STR));
        int exit_code = 0;
        wait(&exit_code);
        assert(exit_code == 0);
    }

    return 0;
}
```

#### 详细分析

**步骤 1：创建管道**

```c
uint64 pipe_fd[2];
int ret = pipe(&pipe_fd);
```

**结果**：
- `pipe_fd[0]`：读端文件描述符
- `pipe_fd[1]`：写端文件描述符
- 两个描述符都指向同一个管道缓冲区

**步骤 2：创建子进程**

```c
if (fork() == 0) {
    // 子进程代码
} else {
    // 父进程代码
}
```

**fork() 后的状态**：
```
父进程                    子进程
┌─────────────────┐       ┌─────────────────┐
│ pipe_fd[0] (读) │ ────→  │ pipe_fd[0] (读) │
│ pipe_fd[1] (写) │ ────→  │ pipe_fd[1] (写) │
└─────────────────┘       └─────────────────┘
        ↓                         ↓
    同一个管道缓冲区 ←──────────┘
```

**关键点**：
- 父子进程都有管道的读写端
- 文件描述符编号相同
- 共享同一个管道结构

**步骤 3：子进程读取**

```c
// 子进程
char buffer[32 + 1];
read(pipe_fd[0], buffer, 32);
assert(strncmp(buffer, STR, strlen(STR)) == 0);
exit(0);
```

**执行流程**：
1. 尝试从 `pipe_fd[0]`（读端）读取最多 32 字节
2. 如果管道为空，**阻塞**等待数据
3. 父进程写入数据后，子进程被唤醒
4. 读取数据到 buffer
5. 比较读取的数据与 STR 是否一致
6. 退出子进程

**步骤 4：父进程写入**

```c
// 父进程
write(pipe_fd[1], STR, strlen(STR));
int exit_code = 0;
wait(&exit_code);
assert(exit_code == 0);
```

**执行流程**：
1. 向 `pipe_fd[1]`（写端）写入 STR 的内容
2. 数据进入管道缓冲区
3. 子进程从阻塞中唤醒，开始读取
4. 等待子进程结束
5. 检查子进程退出码

#### 完整执行时序

```
时间线：
─────────────────────────────────────────────────────────

T1: 父进程创建管道
    pipe_fd[0] ─┐
    pipe_fd[1] ─┼──→ [空管道缓冲区]

T2: fork() 创建子进程
    父进程和子进程都拥有 pipe_fd[0] 和 pipe_fd[1]

T3: 子进程尝试读取
    read(pipe_fd[0], buffer, 32)
    ↓
    管道为空，子进程阻塞 ⏸

T4: 父进程写入数据
    write(pipe_fd[1], "hello pipe!", 12)
    ↓
    [管道缓冲区] = "hello pipe!"

T5: 子进程被唤醒
    从管道读取 "hello pipe!"
    ↓
    buffer = "hello pipe!"

T6: 数据验证
    strncmp(buffer, STR, ...) == 0 ✓
    子进程 exit(0)

T7: 父进程回收子进程
    wait(&exit_code)
    exit_code == 0 ✓
```

### 5.3 关键设计要点

#### 1. 为什么需要关闭未使用的端？

**问题代码**：
```c
if (fork() == 0) {
    read(pipe_fd[0], buffer, 32);   // 只用读端
    // 忘记关闭 pipe_fd[1]!
} else {
    write(pipe_fd[1], STR, strlen(STR));  // 只用写端
    // 忘记关闭 pipe_fd[0]!
}
```

**改进版本**：
```c
if (fork() == 0) {
    close(pipe_fd[1]);  // 关闭未使用的写端
    read(pipe_fd[0], buffer, 32);
    close(pipe_fd[0]);
    exit(0);
} else {
    close(pipe_fd[0]);  // 关闭未使用的读端
    write(pipe_fd[1], STR, strlen(STR));
    close(pipe_fd[1]);
    wait(NULL);
}
```

**为什么要关闭？**

**问题 1：资源泄漏**
- 每个打开的文件描述符都占用资源
- 不关闭会导致文件结构无法释放

**问题 2：影响 EOF 判断**
```c
// 子进程
read(pipe_fd[0], buffer, 32);  // 等待数据
// 如果写端还打开（被子进程持有），read 会一直阻塞
// 即使父进程已经关闭了它的写端！

// 正确行为：
// - 当所有写端都关闭时，read 返回 0 (EOF)
// - 如果子进程持有写端，read 永远看不到 EOF
```

**问题 3：死锁风险**
```c
// 如果父子进程都持有读写端
// 父进程：write 写端 → 阻塞（管道满）
// 子进程：write 写端 → 阻塞（管道满）
// 结果：两个进程都在等待对方读取，死锁！
```

#### 2. 缓冲区大小的影响

**小缓冲区**：
```c
#define PIPESIZE 1024  // 1KB

// 写入超过缓冲区的数据
write(pipe_fd[1], large_data, 10000);  // 分多次写入
// 第一次写入 1024 字节后阻塞
// 等待子进程读取腾出空间
```

**大缓冲区**：
```c
#define PIPESIZE 65536  // 64KB

// 小于 64KB 的写入通常不会阻塞
write(pipe_fd[1], small_data, 1000);  // 立即返回
```

#### 3. 阻塞 vs 非阻塞

**默认阻塞模式**：
```c
// 管道默认是阻塞的
char buf[100];
int n = read(pipe_fd[0], buf, 100);  // 空管道时阻塞
```

**非阻塞模式**：
```c
// 使用 fcntl 设置非阻塞
fcntl(pipe_fd[0], F_SETFL, O_NONBLOCK);

char buf[100];
int n = read(pipe_fd[0], buf, 100);
if (n < 0 && errno == EAGAIN) {
    // 管道为空，但不会阻塞
    // 可以稍后重试
}
```

### 5.4 实际应用场景

#### 场景 1：Shell 管道

```bash
cmd1 | cmd2
```

**实现原理**：
```c
int pipe_fd[2];
pipe(pipe_fd);

if (fork() == 0) {
    // cmd1
    dup2(pipe_fd[1], STDOUT_FILENO);  // 标准输出重定向到管道
    close(pipe_fd[0]);
    close(pipe_fd[1]);
    exec("cmd1");
} else {
    if (fork() == 0) {
        // cmd2
        dup2(pipe_fd[0], STDIN_FILENO);   // 标准输入重定向到管道
        close(pipe_fd[0]);
        close(pipe_fd[1]);
        exec("cmd2");
    }
    close(pipe_fd[0]);
    close(pipe_fd[1]);
    wait(NULL);
    wait(NULL);
}
```

#### 场景 2：父子进程协作

```c
// 父进程生成数据，子进程处理数据
int pipe_fd[2];
pipe(pipe_fd);

if (fork() == 0) {
    // 子进程：数据处理
    close(pipe_fd[1]);
    int data;
    while (read(pipe_fd[0], &data, sizeof(data)) > 0) {
        process(data);
    }
    close(pipe_fd[0]);
    exit(0);
} else {
    // 父进程：数据生成
    close(pipe_fd[0]);
    for (int i = 0; i < 100; i++) {
        write(pipe_fd[1], &i, sizeof(i));
    }
    close(pipe_fd[1]);
    wait(NULL);
}
```

---

**下一章预告**：我们将学习管道的读写操作实现，包括 piperead() 和 pipewrite()，以及管道的阻塞和唤醒机制。

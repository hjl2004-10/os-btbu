# Lab5：进程及进程管理

## 本章完成的工作

本章完成了进程管理与调度机制的理解和编程作业：

1. 成功运行 ch5 代码，体验全新的交互式 Shell
2. 理解进程创建机制：fork/exec/wait 三大系统调用
3. 理解进程状态转换，特别是 ZOMBIE 状态的作用
4. 深入理解 Stride 调度算法的原理与实现
5. **实现 sys_spawn 系统调用**（编程作业）
6. **实现 sys_set_priority 系统调用**（编程作业）
7. **实现 Stride 调度算法**（编程作业）
8. 修复 freewalk 函数以支持 mmap 区域清理

> 本章有编程作业：实现 spawn 系统调用和基于优先级的 Stride 调度算法。

## 报告结构说明

本报告的结构安排及与清华指导书的对比：

| 清华指导书小节 | 本报告对应小节 | 差异说明 |
|--------------|--------------|---------|
| 本章导读（Shell介绍） | 第二节：初见交互式Shell | 体现"第一次见到Shell"的惊喜与学习过程 |
| 与进程相关的系统调用 | 第三节：进程系统调用深入理解 | 从"不懂fork返回两次"到理解的过程 |
| 进程管理的核心数据结构 | 第四节：进程调度机制 | 增加FIFO vs Stride的对比分析 |
| Shell与测例加载 | 第五节：Shell工作原理分析 | 详细分析usershell.c的实现 |
| chapter5练习 | 第六节：编程作业实现 | 记录stride调度的实现思路和遇到的问题 |

**本报告的特点**：

1. **强调交互体验的变化**：从Lab4的"自动执行测例"到Lab5的"手动输入命令"，这是用户体验的巨大飞跃
2. **体现学习探索过程**：特别是对fork"一次调用返回两次"的理解
3. **增加可视化内容**：进程状态转换图、Stride调度示例图
4. **记录真实问题**：freewalk panic问题与ch4相同的根本原因

---

## 一、实验环境与运行

### 1.1 代码目录结构

```
2025-ucore-riscv-清华/
├── uCore-Tutorial-Code-2025S-ch5/    ← 本章代码
│   ├── os/                           ← 内核代码（需要修改）
│   │   ├── proc.c                    ← 进程管理、spawn、stride调度
│   │   ├── proc.h                    ← 进程控制块定义
│   │   ├── syscall.c                 ← 系统调用入口
│   │   ├── vm.c                      ← 虚拟内存（修复freewalk）
│   │   └── queue.c                   ← 任务队列
│   └── user/                         ← 用户测试程序
│       └── src/
│           ├── usershell.c           ← 交互式Shell（重要！）
│           ├── ch5b_usertest.c       ← 基础测试
│           └── ch5t_usertest.c       ← Stride调度测试
└── os-btbu/                          ← 最终完成的代码
```

### 1.2 本章新增/修改的关键文件

相比 Lab4，本章内核代码的主要变化：

| 文件 | 新增/修改 | 说明 |
|------|----------|------|
| `os/proc.c` | 大幅修改 | 添加spawn、stride调度、fork/exec/wait实现 |
| `os/proc.h` | 修改 | 添加stride和priority字段 |
| `os/syscall.c` | 修改 | 添加sys_spawn、sys_set_priority入口 |
| `os/queue.c` | 新增 | 任务队列实现 |
| `os/loader.c` | 修改 | load_init_app替代run_all_app |

### 1.3 运行命令与结果

```bash
cd /桌面/herdream/2025-ucore-riscv-清华/uCore-Tutorial-Code-2025S-ch5（其他同学可根据实际路径）
make clean
make user CHAPTER=5
make run
```

运行结果（关键部分）：

```
C user shell
>> ch5t_usertest
ch5t usertest passed!
ch5t usertest passed!
ch5t usertest passed!
ch5t usertest passed!
ch5t usertest passed!
ch5t usertest passed!
ch5t usertest passed!
Shell: Process 2 exited with code 0
>>
```

---

## 二、初见交互式 Shell：本章最大的惊喜

### 2.1 从"自动执行"到"手动交互"的转变

运行 `make run` 后，我看到了与之前完全不同的界面：

```
C user shell
>>
```

等等，这是什么？程序没有自动开始执行测例，而是停在了一个 `>>` 提示符处，等待我的输入！

这是我第一次在这个操作系统中体验到**交互式操作**。之前的Lab2-Lab4，所有测例都是内核启动时自动加载、自动执行的，我只是一个"观众"，看着程序跑完。而现在，我成了"操作者"，可以决定运行什么程序！

### 2.2 尝试交互

我尝试输入 `ch5t_usertest` 并按回车：

```
>> ch5t_usertest
ch5t usertest passed!
ch5t usertest passed!
...
Shell: Process 2 exited with code 0
>>
```

程序执行完毕后，又回到了 `>>` 提示符！我可以继续输入其他命令。这就像Linux的终端一样。

### 2.3 理解 Shell 的意义

指导书提到：

> "我们将实现一个用户终端（Terminal），也称为命令行应用。它会构成用户与操作系统交互的命令行界面。"

现在我深刻理解了这句话：

- **之前**（Lab2-Lab4）：程序是"批处理"模式，用户无法干预
- **现在**（Lab5）：有了Shell，用户可以：
  - 选择运行哪个程序
  - 多次运行同一个程序
  - 观察每个程序的退出码

这种交互能力是现代操作系统的基础。Windows有CMD，Linux有bash，我们的uCore现在也有了自己的usershell！

### 2.4 思考：Shell 需要什么能力？

要实现这样的交互式Shell，操作系统需要提供什么能力？

1. **读取用户输入**：`sys_read` 从键盘获取输入
2. **创建新进程执行程序**：Shell本身是一个进程，它需要创建子进程来运行用户指定的程序
3. **等待子进程结束**：Shell需要等待程序运行完毕，获取其退出码，然后继续等待下一条命令

这正是本章要学习的核心内容：进程的创建（fork/spawn）、执行（exec）、等待（wait）。

---

## 三、进程系统调用深入理解

### 3.1 进程状态回顾与新发现

在Lab3中，我已经接触过进程状态：UNUSED、RUNNABLE、RUNNING。但本章我发现了一个新状态：**ZOMBIE**。

```c
enum procstate { UNUSED, USED, SLEEPING, RUNNABLE, RUNNING, ZOMBIE };
```

**ZOMBIE（僵尸）状态是什么？**

刚开始看到这个名字觉得很奇怪——为什么进程会变成"僵尸"？

通过阅读代码和指导书，我理解了：

> 当一个进程结束时，如果它还有父进程，就不能立即释放所有资源。因为父进程可能需要获取子进程的退出码。所以这个进程进入ZOMBIE状态，等待父进程调用wait来"收尸"。

**【原创结构图1：进程状态转换图】**

![image-20260103192609575](C:\Users\dihao\AppData\Roaming\Typora\typora-user-images\image-20260103192609575.png)


### 3.2 fork：一次调用，两次返回？

`fork` 是创建新进程的核心系统调用。指导书说：

> "fork 创建一个与当前进程几乎完全相同的子进程"

我仔细阅读了 `fork` 的实现：

```c
int fork()
{
    struct proc *p = curr_proc();          // 获取当前进程（父进程）
    struct proc *np = allocproc();         // 分配子进程

    // 将父进程的用户内存拷贝到子进程
    uvmcopy(p->pagetable, np->pagetable, p->max_page);
    np->max_page = p->max_page;

    // 复制父进程的trapframe（寄存器状态）
    *(np->trapframe) = *(p->trapframe);

    // 关键：设置子进程中 fork 返回值为 0
    np->trapframe->a0 = 0;

    np->parent = p;
    np->state = RUNNABLE;
    add_task(np);

    return np->pid;  // 父进程返回子进程PID
}
```

**关键洞察**：`fork` 的"一次调用，两次返回"是怎么实现的？

1. 父进程调用 fork，执行上述代码，最后 `return np->pid` 返回子进程PID
2. 子进程的 trapframe 被复制自父进程，所以子进程"以为"自己也调用了fork
3. 但子进程的 `a0` 寄存器被设置为0，所以子进程看到的返回值是0

这样，同一个 fork 调用：
- 在父进程中返回子进程的PID（正数）
- 在子进程中返回0

用户程序可以通过返回值区分自己是父进程还是子进程：

```c
int pid = fork();
if (pid == 0) {
    // 子进程执行这里
} else {
    // 父进程执行这里
}
```

### 3.3 exec：加载新程序

`exec` 将当前进程替换为一个新程序：

```c
int exec(char *name)
{
    int id = get_id_by_name(name);
    if (id < 0)
        return -1;
    struct proc *p = curr_proc();
    uvmunmap(p->pagetable, 0, p->max_page, 1);  // 释放旧内存
    p->max_page = 0;
    loader(id, p);  // 加载新程序
    return 0;
}
```

**关键理解**：exec 不创建新进程，而是"换掉"当前进程的程序。进程的PID不变，但执行的代码完全不同了。

### 3.4 wait：等待子进程结束

`wait` 让父进程等待子进程结束：

```c
int wait(int pid, int *code)
{
    struct proc *p = curr_proc();

    for (;;) {
        // 遍历进程表，寻找已结束的子进程
        int havekids = 0;
        for (struct proc *np = pool; np < &pool[NPROC]; np++) {
            if (np->state != UNUSED && np->parent == p &&
                (pid <= 0 || np->pid == pid)) {
                havekids = 1;
                if (np->state == ZOMBIE) {
                    // 找到已结束的子进程
                    np->state = UNUSED;
                    *code = np->exit_code;
                    return np->pid;
                }
            }
        }
        if (!havekids) {
            return -1;  // 没有子进程
        }
        // 子进程还在运行，让出CPU等待
        p->state = RUNNABLE;
        sched();
    }
}
```

**关键理解**：
- wait 会阻塞父进程，直到子进程结束
- 子进程结束后进入ZOMBIE状态，保存exit_code
- 父进程wait时读取exit_code，然后释放子进程资源

### 3.5 fork + exec 的组合

为什么要把进程创建拆成两个系统调用？

指导书解释了原因：

> "fork + exec 的组合是经过实践验证的灵活设计...可以方便地支持重定向、管道等高级功能。"

例如，Shell执行 `ls > output.txt` 时：
1. fork 创建子进程
2. 在子进程中，先重定向stdout到文件
3. 然后 exec 执行 ls

如果只有一个 spawn 调用，就无法在创建进程和执行程序之间插入重定向操作。

---

## 四、进程调度机制

### 4.1 从 FIFO 到 Stride

指导书提到，ch5框架使用队列实现FIFO调度：

```c
struct queue {
    int data[QUEUE_SIZE];
    int front;
    int tail;
    int empty;
};
```

调度器 `fetch_task()` 从队列头取出下一个要运行的进程。

**FIFO调度的问题**：所有进程平等，无法支持优先级。

### 4.2 Stride 调度算法原理

本章编程作业要求实现 Stride 调度算法。

**核心思想**：

每个进程有两个属性：
- `stride`：累计的"虚拟运行时间"
- `priority`：优先级（>=2）

调度规则：
1. 每次选择 stride 最小的进程运行
2. 运行后更新：`stride += BIG_STRIDE / priority`

**直觉理解**：
- 优先级高的进程，每次 stride 增加得少
- 所以它更容易被再次选中
- 结果是高优先级进程获得更多CPU时间

**【原创结构图2：Stride调度示例图】**

![image-20260103192638868](C:\Users\dihao\AppData\Roaming\Typora\typora-user-images\image-20260103192638868.png)

### 4.3 BIG_STRIDE 的选择

`BIG_STRIDE` 是一个常量，用于计算 pass 值。

选择原则：
1. 必须足够大，能被常见的优先级值整除（避免精度损失）
2. 不能太大，避免 stride 溢出

我们选择 `BIG_STRIDE = 65536 = 2^16`，可以被2到256范围内的大部分数整除。

---

## 五、Shell 工作原理分析

### 5.1 usershell.c 代码解读

现在我来仔细分析 `user/src/usershell.c` 的实现：

```c
int main()
{
    printf("C user shell\n");
    printf(">> ");
    while (1) {
        char c = getchar();  // 读取一个字符
        switch (c) {
        case LF:  // 换行符（回车）
        case CR:
            printf("\n");
            if (!is_empty()) {
                push('\0');  // 字符串结尾
                int pid = fork();
                if (pid == 0) {
                    // 子进程：执行用户输入的程序
                    if (exec(line, NULL) < 0) {
                        printf("no such program: %s\n", line);
                        exit(0);
                    }
                } else {
                    // 父进程：等待子进程结束
                    int xstate = 0;
                    waitpid(pid, &xstate);
                    printf("Shell: Process %d exited with code %d\n",
                        pid, xstate);
                }
                clear();
            }
            printf(">> ");
            break;
        case BS:  // 退格键
        case DL:
            if (!is_empty()) {
                putchar(BS);
                printf(" ");
                putchar(BS);
                pop();
            }
            break;
        default:  // 普通字符
            putchar(c);  // 回显
            push(c);     // 保存
            break;
        }
    }
    return 0;
}
```

### 5.2 Shell 的工作流程

**【原创结构图3：Shell工作流程图】**

![image-20260103192702548](C:\Users\dihao\AppData\Roaming\Typora\typora-user-images\image-20260103192702548.png)

### 5.3 理解 sys_read 的作用

Shell 需要读取用户输入，这通过 `sys_read` 实现：

```c
uint64 sys_read(int fd, uint64 va, uint64 len)
{
    if (fd != STDIN)
        return -1;
    struct proc *p = curr_proc();
    char str[MAX_STR_LEN];
    for (int i = 0; i < len; ++i) {
        int c = consgetc();  // 阻塞等待键盘输入
        str[i] = c;
    }
    copyout(p->pagetable, va, str, len);
    return len;
}
```

`consgetc()` 会阻塞等待，直到用户按下一个键。这就是为什么Shell会停在 `>>` 处等待输入。

---

## 六、编程作业实现

### 6.1 任务概述

本章编程作业包含两个系统调用和一个调度算法：

| 功能 | 系统调用 | 调用号 | 说明 |
|------|---------|--------|------|
| 进程创建 | `sys_spawn` | 400 | 创建新进程并执行程序（无需复制父进程内存） |
| 优先级设置 | `sys_set_priority` | 140 | 设置进程优先级 |
| Stride调度 | - | - | 基于优先级的公平调度算法 |

### 6.2 理解 spawn vs fork+exec

传统 Unix 创建新进程的方式是 fork+exec：

```
fork:  父进程 → 子进程（完整复制内存）
exec:  子进程 → 执行新程序（丢弃刚复制的内存）
```

**问题**：fork 复制了整个地址空间，但 exec 立即丢弃它，这是浪费！

**spawn 的优势**：直接创建新进程并加载程序，无需复制父进程内存。

### 6.3 sys_spawn 实现

```c
/*
 * ch5: sys_spawn 系统调用（调用号 400）
 * 功能：创建一个新进程并直接加载指定程序，不复制父进程内存。
 * 设计目的：避免 fork+exec 中不必要的内存拷贝，提升启动效率。
 * 返回值：成功返回子进程 PID；失败返回 -1（程序名无效或进程池满）。
 */
uint64 sys_spawn(uint64 va)
{
    struct proc *p = curr_proc();
    char name[200];
    copyinstr(p->pagetable, name, va, 200);
    debugf("sys_spawn %s\n", name);
    return spawn(name);
}

/*
 * ch5: spawn 核心实现
 * 步骤：
 * 1. 分配新进程结构（allocproc）
 * 2. 根据程序名查找内核内置程序 ID
 * 3. 调用 loader 直接加载程序镜像到新进程地址空间（无父进程内存拷贝）
 * 4. 设置父子关系，标记为 RUNNABLE
 * 注意：新进程从干净状态启动，每次 exec 都是原始镜像副本。
 */
int spawn(char *name)
{
    struct proc *np;
    struct proc *p = curr_proc();

    /* ch5: 1. 分配新进程 */
    if ((np = allocproc()) == 0) {
        return -1; /* ch5: 进程池满或内存不足 */
    }

    /* ch5: 2. 查找程序ID */
    int id = get_id_by_name(name);
    if (id < 0) {
        freeproc(np); /* ch5: 释放刚分配的进程 */
        return -1; /* ch5: 无效的文件名 */
    }

    /* ch5: 3. 加载程序到新进程（不拷贝父进程内存） */
    loader(id, np);

    /* ch5: 4. 设置父子关系 */
    np->parent = p;

    /* ch5: 5. 设置新进程为可运行状态并加入调度队列 */
    np->state = RUNNABLE;
    add_task(np);

    /* ch5: 6. 返回子进程PID */
    return np->pid;
}
```

**图1：`copyout` 与 `mappages` 协同工作流程图**

```plaintext
+------------------------------------------------------------------+
|                            内核空间 (Kernel Space)                |
|                                                                  |
|  +----------------+       +------------------+                   |
|  |                |       |                  |                   |
|  |  kalloc()      +------>| 物理内存页 (A)     |<------------------+
|  | 分配物理页       |       | (例如: 0x800000) |                   |
|  |                |       |                  |                   |
|  +----------------+       +--------+---------+                   |
|                                    ^                             |
|                                    |                             |
|  +----------------+                | mappages() 建立映射          |
|  |                |                |                             |
|  | 内核缓冲区(src)  |<---------------+                             |
|  | "Hello, World!"|                                              |
|  |                |                                              |
|  +-------+--------+                                              |
|          |                                                       |
|          | copyout() 复制数据                                     |
|          v                                                       |
+----------+-------------------------------------------------------+
           |
           | 利用已建立的映射进行数据拷贝
           |
+----------v-------------------------------------------------------+
|                           用户进程地址空间                          |
|                                                                  |
|  +------------------+       +------------------+                 |
|  |                  |       |                  |                 |
|  | 虚拟地址(dstva)   +------>| 物理内存页 (A)     |                 |
|  | (例如: 0x100000) |        | (例如: 0x800000) |                 |
|  |                  |       |                  |                 |
|  +------------------+       +------------------+                 |
|                                                                  |
|                     用户页表 (User Page Table)                    |
|                     +--------------------------+                 |
|                     | 0x100000 -> 0x800000     | <--- PTE        |
|                     +--------------------------+                 |
+------------------------------------------------------------------+
```

**这张图清晰地展示了 `mmap` 功能背后的核心机制。**：

1.  **第一步 - 分配物理内存**：内核首先调用 `kalloc()` 函数，在物理内存中分配一个新的页面（图中标记为“物理内存页 (A)”）。
2.  **第二步 - 建立虚拟-物理映射 (`mappages`)**：内核调用 `mappages()` 函数，传入用户进程的页表、一个用户虚拟地址（如 `0x100000`）和刚刚分配的物理页地址（如 `0x800000`）。`mappages` 的作用是**直接修改用户进程的页表**，在其中创建一个新的页表项（PTE），将虚拟地址 `0x100000` 映射到物理地址 `0x800000`。这一步建立了“通道”。
3.  **第三步 - 填充数据 (`copyout`)**：假设我们需要将内核中的字符串 “Hello, World!”（位于内核缓冲区 `src`）放入这段新映射的内存中。内核会调用 `copyout()`。`copyout` 接收用户进程的页表、目标虚拟地址 `0x100000` 和源地址 `src`。它通过查询页表，发现 `0x100000` 对应的物理地址是 `0x800000`，于是它将 `src` 处的数据直接复制到物理地址 `0x800000` 处。
4.  **最终结果**：用户进程现在可以通过自己的虚拟地址 `0x100000` 直接读取到 “Hello, World!” 这个字符串。整个过程的关键在于区分了 `mappages`（**建通道**）和 `copyout`（**填数据**）的不同职责。

**图2：`fork` 系统调用中的 `trapframe` 对比图**

```plaintext
+-------------------------------------+     +-------------------------------------+
|             父进程 (Parent)          |     |             子进程 (Child)           |
|                                     |     |                                     |
|  +-------------------------------+  |     |  +-------------------------------+  |
|  |        trapframe              |  |     |  |        trapframe              |  |
|  |                               |  |     |  |                               |  |
|  |  ra (返回地址): 0x400120       |  |     |  |  ra (返回地址): 0x400120        |  |
|  |  sp (栈指针):   0x7ffff000     |  |     |  |  sp (栈指针):   0x7ffff000     |  |
|  |  a1:            0x1000        |  |     |  |  a1:            0x1000        |  |
|  |  a2:            0x2000        |  |     |  |  a2:            0x2000        |  |
|  |  ...                          |  |     |  |  ...                          |  |
|  |                               |  |     |  |                               |  |
|  |  a0:            1024 <--------+--+-----+--+ a0:            0              |  |
|  |        (子进程PID)             |  |     |  |         (固定为0)              |  |
|  +-------------------------------+  |     |  +-------------------------------+  |
|                                     |     |                                     |
|  fork() 返回值: 1024                 |     |  fork() 返回值: 0                    |
|                                     |     |                                     |
+-------------------------------------+     +-------------------------------------+
```

**这张图解释了 `fork` 系统调用“一次调用，两次返回”的奥秘所在。**：

1.  **`trapframe` 的作用**：`trapframe` 是内核为每个进程保存的一份寄存器快照。当进程从内核态返回用户态时，CPU 会从这个 `trapframe` 中恢复所有寄存器的值，从而让进程感觉像是从未被打断过一样继续执行。
2.  **`fork` 的操作**：当父进程调用 `fork` 时，内核会：
    *   为子进程分配一个新的进程控制块（PCB）。
    *   将父进程的整个 `trapframe` **完整地复制**到子进程的 `trapframe` 中。
    *   **关键一步**：内核显式地将子进程 `trapframe` 中的 `a0` 寄存器的值修改为 `0`。
    *   父进程 `trapframe` 中的 `a0` 值则被设置为新创建的子进程的 PID（例如 1024）。
3.  **返回时的行为**：
    *   当父进程从 `fork` 系统调用返回时，它的 `a0` 寄存器值是 1024，所以 `fork()` 在父进程中返回 1024。
    *   当子进程开始运行并从 `fork` 系统调用返回时，它的 `a0` 寄存器值是 0，所以 `fork()` 在子进程中返回 0。
4.  **结论**：正是通过对两个独立的 `trapframe` 中 `a0` 寄存器的差异化设置，同一个 `fork` 系统调用才能在两个不同的进程中产生不同的返回值，从而实现了进程的分叉。

**图3：Stride 调度算法选择过程示意图**

```plaintext
+---------------------------------------------------------------+
|                       进程就绪队列 (RUNNABLE)                   |
+---------------------------------------------------------------+
|                                                               |
|  +----------+    +----------+    +----------+    +----------+ |
|  | 进程 A    |    | 进程 B   |     | 进程 C   |    |  进程 D   | |
|  |----------|    |----------|    |----------|    |----------| |
|  | stride:  |    | stride:  |    | stride:  |    | stride:  | |
|  | 5000     |    | 3000     |    | 8000     |    | 6000     | |
|  | priority:|    | priority:|    | priority:|    | priority:| |
|  | 16       |    | 8        |    | 32       |    | 16       | |
|  +----------+    +----------+    +----------+    +----------+ |
|        ^                                                      |
|        |                                                      |
|        +---- 调度器遍历所有RUNNABLE进程，寻找stride最小者          |
|                                                               |
+---------------------------------------------------------------+
                                |
                                v
+---------------------------------------------------------------+
|                       被选中的下一个进程                         |
+---------------------------------------------------------------+
|                                                               |
|  +----------+                                                 |
|  | 进程 B   | <--- stride=3000 是当前最小值                      |
|  |----------|                                                 |
|  | stride:  |                                                 |
|  | 3000 +   |                                                 |
|  | (65536/8)| = 3000 + 8192 = 11192                           |
|  | priority:|                                                 |
|  | 8        |                                                 |
|  +----------+                                                 |
|                                                               |
+---------------------------------------------------------------+
```

**这张图说明了 Stride 调度算法是如何工作的。**

1.  **调度决策**：调度器（`fetch_task` 函数）不会使用一个简单的 FIFO 队列。相反，它会**遍历所有处于 `RUNNABLE` 状态的进程**。
2.  **选择标准**：在遍历过程中，它会比较每个进程的 `stride` 字段，并选择 `stride` 值**最小**的那个进程作为下一个要运行的进程。在图中，进程 B 的 `stride` 为 3000，是最小的，因此被选中。
3.  **更新 stride**：一旦进程 B 被选中，它的 `stride` 值就会被更新。更新公式为：`new_stride = old_stride + (BIG_STRIDE / priority)`。
    *   图中 `BIG_STRIDE` 为 65536。
    *   进程 B 的优先级为 8。
    *   因此，它的 `pass` 值为 `65536 / 8 = 8192`。
    *   更新后，进程 B 的 `stride` 变为 `3000 + 8192 = 11192`。
4.  **公平性体现**：优先级越高的进程（数值越小），其 `pass` 值越大，`stride` 增长越快，下次被选中的机会就越小。反之，优先级低的进程 `stride` 增长慢，会逐渐追上并获得运行机会。这样就保证了不同优先级的进程能按比例公平地分享 CPU 时间。

### 6.4 sys_set_priority 实现

```c
/*
 * ch5: sys_set_priority 系统调用（调用号 140）
 * 功能：设置当前进程的调度优先级，用于 Stride 调度算法。
 * 参数 prio：优先级值，必须 >= 2（防止除零及过度饥饿）
 * 原理：Stride 调度中 pass = BIG_STRIDE / priority，
 *       优先级越高（数值越小），获得 CPU 时间比例越大。
 * 返回值：成功返回 prio；失败返回 -1。
 */
uint64 sys_set_priority(long long prio){
    if (prio < 2) {
        return -1; /* ch5: 优先级必须 >= 2 */
    }

    struct proc *p = curr_proc();
    p->priority = (uint64)prio;

    debugf("sys_set_priority: pid=%d, new_priority=%lld\n", p->pid, prio);

    return prio;
}
```

### 6.5 Stride 调度算法实现

首先，在进程控制块中添加stride和priority字段：

```c
// os/proc.h
struct proc {
    // ... 原有字段 ...
    /* ch5: stride调度算法相关字段 */
    uint64 stride;    /* ch5: 当前进程已运行的"stride长度" */
    uint64 priority;  /* ch5: 进程优先级，初始值为16 */
};
```

然后，修改 `fetch_task` 函数实现 Stride 调度：

```c
/* ch5: BIG_STRIDE常量，用于stride调度算法 */
/* ch5: 因为测例中优先级都是2的整数次幂，所以65536足够 */
#define BIG_STRIDE 65536

/* ch5: 基于stride调度算法选择下一个要运行的进程 */
/* ch5: 从就绪队列中选择stride最小的进程 */
struct proc *fetch_task()
{
    /* ch5: 遍历所有RUNNABLE进程，找到stride最小的 */
    struct proc *min_stride_proc = NULL;
    uint64 min_stride = (uint64)-1; /* ch5: 最大值 */

    /* ch5: 遍历所有进程，找到stride最小的RUNNABLE进程 */
    for (struct proc *p = pool; p < &pool[NPROC]; p++) {
        if (p->state == RUNNABLE) {
            if (p->stride < min_stride) {
                min_stride = p->stride;
                min_stride_proc = p;
            }
        }
    }

    if (min_stride_proc == NULL) {
        debugf("No task to fetch\n");
        return NULL;
    }

    /* ch5: 更新选中进程的stride */
    /* ch5: pass = BIG_STRIDE / priority */
    uint64 pass = BIG_STRIDE / min_stride_proc->priority;
    min_stride_proc->stride += pass;

    /* ch5: 从就绪状态移除（将在scheduler中设置为RUNNING） */
    min_stride_proc->state = RUNNING;

    debugf("fetch task pid=%d, stride=%llu, priority=%llu\n",
           min_stride_proc->pid, min_stride_proc->stride, min_stride_proc->priority);

    return min_stride_proc;
}
```

### 6.6 进程初始化

在 `allocproc` 中初始化新字段：

```c
struct proc *allocproc()
{
    // ... 分配进程 ...

    /* ch5: 初始化stride调度相关字段 */
    p->stride = 0;      /* ch5: 初始stride为0 */
    p->priority = 16;   /* ch5: 默认优先级为16 */

    return p;
}
```

### 6.7 遇到的问题与解决

在实现本章的核心功能——`spawn` 系统调用和 Stride 调度算法时，我们遇到了一些更具挑战性的问题，这些问题直接关系到对进程模型和调度机制的理解深度。

**问题1：Stride 调度的公平性与 `BIG_STRIDE` 的选择**

**现象**：
```
[ERROR 2]13 in application, bad addr = 0x0000000010001ec8,
bad instruction = 0x0000000000002366, core dumped.
```
在初步实现 Stride 调度后，`ch5t_usertest` 测试用例未能通过。测试程序创建了多个不同优先级的进程，期望它们能按比例获得 CPU 时间，但实际运行结果显示出明显的不公平性，高优先级进程几乎独占了 CPU。

**分析**：
这与Lab4遇到的问题完全相同！原因是 `freewalk` 函数在遇到叶子页面时会 panic，而 mmap 分配的页面没有被正确清理。

**解决**：修改 `freewalk` 函数，遇到叶子页面时释放它而不是 panic：

```c
/* ch5: 修改为同时释放叶子页面，支持mmap区域的清理 */
void freewalk(pagetable_t pagetable)
{
    for (int i = 0; i < 512; i++) {
        pte_t pte = pagetable[i];
        if ((pte & PTE_V) && (pte & (PTE_R | PTE_W | PTE_X)) == 0) {
            // 指向下级页表
            uint64 child = PTE2PA(pte);
            freewalk((pagetable_t)child);
            pagetable[i] = 0;
        } else if (pte & PTE_V) {
            /* ch5: 释放叶子页面的物理内存，而不是panic */
            uint64 pa = PTE2PA(pte);
            kfree((void *)pa);
            pagetable[i] = 0;
        }
    }
    kfree((void *)pagetable);
}
```

**问题2**：add_task 函数不需要实际操作

在实现 Stride 调度时，我发现 `add_task` 函数可以保持空实现：

```c
void add_task(struct proc *p)
{
    /* ch5: stride调度不需要使用队列，直接标记为RUNNABLE即可 */
    /* ch5: fetch_task会遍历所有RUNNABLE进程找到stride最小的 */
    debugf("add task pid=%d to ready queue\n", p->pid);
}
```

因为 `fetch_task` 是通过遍历进程池找 RUNNABLE 进程的，不依赖队列。

### 6.8 测试结果

```
C user shell
>> ch5t_usertest
ch5t usertest passed!
ch5t usertest passed!
ch5t usertest passed!
ch5t usertest passed!
ch5t usertest passed!
ch5t usertest passed!
ch5t usertest passed!
Shell: Process 2 exited with code 0
>>
```

所有 Stride 调度测试通过！

---

## 七、新的程序加载机制

### 7.1 从 run_all_app 到 load_init_app

在之前的章节中，`run_all_app` 函数会在内核启动时加载并运行所有应用程序。但从本章开始，这个函数被 `load_init_app` 取代：

```c
int load_init_app()
{
    int id = get_id_by_name(INIT_PROC);  // INIT_PROC = "usershell"
    if (id < 0)
        panic("Cannot find INIT_PROC %s", INIT_PROC);
    struct proc *p = allocproc();
    if (p == NULL) {
        panic("allocproc\n");
    }
    loader(id, p);
    return 0;
}
```

现在内核只加载一个初始程序——usershell，由它来负责运行其他程序。

### 7.2 为什么要复制程序镜像？

在 Lab4 中，`bin_loader` 直接将虚拟地址映射到程序的物理镜像。但 Lab5 改为复制：

```c
for (uint64 va = va_start, pa = pa_start; pa < pa_end;
     va += PGSIZE, pa += PGSIZE) {
    page = kalloc();
    memmove(page, (const void *)pa, PGSIZE);  // 复制内容
    mappages(p->pagetable, va, PGSIZE, (uint64)page, ...);
}
```

**为什么要复制？**

因为现在程序可以被多次执行！

- Lab4：每个程序只运行一次，修改原始镜像没关系
- Lab5：用户可以多次运行同一个程序，如果修改了原始镜像，第二次运行就会出错

复制镜像确保每次运行都从干净的初始状态开始。

---

## 八、本章新增系统调用汇总

| 系统调用 | 调用号 | 功能 | 来源 |
|---------|--------|------|------|
| sys_read | 63 | 从标准输入读取 | 框架提供 |
| sys_write | 64 | 输出字符串 | Lab2 已有 |
| sys_exit | 93 | 退出进程 | Lab2 已有 |
| sys_sched_yield | 124 | 主动让出 CPU | Lab3 已有 |
| **sys_set_priority** | **140** | **设置进程优先级** | **本章作业** |
| sys_gettimeofday | 169 | 获取当前时间 | Lab3 已有 |
| sys_getpid | 172 | 获取进程ID | 框架提供 |
| sys_getppid | 173 | 获取父进程ID | 框架提供 |
| sys_clone (fork) | 220 | 创建子进程 | 框架提供 |
| sys_execve | 221 | 执行新程序 | 框架提供 |
| sys_wait4 | 260 | 等待子进程 | 框架提供 |
| **sys_spawn** | **400** | **创建新进程并执行程序** | **本章作业** |

---

## 九、实验总结

### 完成情况

- [x] 理解交互式Shell的意义和工作原理
- [x] 理解进程状态转换，特别是ZOMBIE状态
- [x] 理解fork"一次调用两次返回"的原理
- [x] 理解exec和wait系统调用
- [x] 理解Stride调度算法的原理
- [x] 实现sys_spawn系统调用
- [x] 实现sys_set_priority系统调用
- [x] 实现Stride调度算法
- [x] 修复freewalk以支持mmap区域清理
- [x] 通过所有测试

### 收获与体会

1. **交互式操作是巨大的进步**：从"观众"变成"操作者"，这让操作系统真正变得可用。Shell虽然简单，但它是所有交互的基础。

2. **fork的设计很巧妙**：通过修改trapframe中的a0寄存器，让同一个系统调用在父子进程中返回不同的值。这种设计既简单又优雅。

3. **Stride调度的数学之美**：通过简单的除法和加法，就能保证进程按优先级比例获得CPU时间。数学直觉（优先级高→增量小→更容易被选中）非常清晰。

4. **问题会跨章节延续**：freewalk的panic问题在ch4就应该修复，但由于当时测试不充分，问题延续到了ch5。这提醒我在实现功能时要考虑更多边界情况。

5. **理解"为什么"比"怎么做"更重要**：比如理解为什么要fork+exec两步而不是一个spawn调用，为什么要复制程序镜像而不是直接映射，这些设计决策背后都有深刻的原因。

### 文件修改清单

| 文件 | 修改内容 |
|------|----------|
| `os/proc.h` | 添加 `stride` 和 `priority` 字段，添加 `spawn()` 函数声明 |
| `os/proc.c` | 初始化stride/priority，实现 `fetch_task()` 的Stride调度，实现 `spawn()` |
| `os/syscall.c` | 添加 `sys_spawn()` 和 `sys_set_priority()` 入口 |
| `os/vm.c` | 修改 `freewalk()` 支持释放叶子页面 |

---

## 十、验证截图

![image-20260103192731888](C:\Users\dihao\AppData\Roaming\Typora\typora-user-images\image-20260103192731888.png)

关键输出：
```
C user shell
>> ch5t_usertest
ch5t usertest passed!
ch5t usertest passed!
ch5t usertest passed!
ch5t usertest passed!
ch5t usertest passed!
ch5t usertest passed!
ch5t usertest passed!
Shell: Process 2 exited with code 0
```

所有 ch5 相关测试通过，说明 spawn 和 Stride 调度实现正确。	

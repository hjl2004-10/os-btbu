# 第五章：进程创建与 Stride 调度算法

## 一、任务目标

本章需要实现两个核心功能：

1. **sys_spawn**：创建新进程并执行指定程序（类似 fork+exec 但更高效）
2. **stride 调度算法**：基于优先级的公平调度，优先级越高获得的 CPU 时间越多

## 二、探索过程

### 2.1 理解 spawn vs fork+exec

传统 Unix 创建新进程的方式是 fork+exec：

```
fork:  父进程 → 子进程（完整复制内存）
exec:  子进程 → 执行新程序（丢弃刚复制的内存）
```

**问题**：fork 复制了整个地址空间，但 exec 立即丢弃它，这是浪费！

**spawn 的优势**：直接创建新进程并加载程序，无需复制父进程内存。

### 2.2 实现 spawn 前需要理解什么？

要实现 spawn，我需要理解现有的 fork 和 exec 是如何工作的：

**fork 的核心步骤**（在 `os/proc.c`）：
```c
int fork() {
    struct proc *np = allocproc();      // 1. 分配新进程
    uvmcopy(p->pagetable, np->pagetable, p->max_page);  // 2. 复制内存（spawn 不需要）
    np->parent = p;                      // 3. 设置父进程
    // ... 复制文件描述符、trapframe 等
    add_task(nt);                        // 4. 加入调度队列
}
```

**exec 的核心步骤**：
```c
int exec(char *path, char **argv) {
    struct inode *ip = namei(path);      // 1. 查找可执行文件
    bin_loader(ip, p);                   // 2. 加载程序到内存
    push_argv(p, argv);                  // 3. 设置命令行参数
}
```

**spawn = allocproc + 设置父进程 + init_stdio + bin_loader + add_task**

### 2.3 关于 Stride 调度算法

**问题背景**：简单的时间片轮转调度对所有进程一视同仁，无法实现"优先级高的进程获得更多CPU时间"。

**Stride 算法的核心思想**：

每个进程有两个属性：
- `stride`：累计的"虚拟运行时间"
- `priority`：优先级（2-255，越大优先级越高）

调度规则：
1. 每次选择 stride 最小的进程运行
2. 运行后更新：`stride += BIG_STRIDE / priority`

**数学直觉**：
- 优先级高的进程，每次 stride 增加得少，更容易被选中
- 优先级为 P 的进程，理论上获得 `P / ΣP` 比例的 CPU 时间

**举例**：
```
进程A: priority=2, 每次 stride += 65536/2 = 32768
进程B: priority=4, 每次 stride += 65536/4 = 16384

初始: A.stride=0, B.stride=0
第1次: 选A或B（stride相等），假设选A，A.stride=32768
第2次: B.stride=0 < A.stride，选B，B.stride=16384
第3次: B.stride=16384 < A.stride，选B，B.stride=32768
第4次: 两者相等，选A或B...

结果：A运行1次，B运行2次，符合 2:4 = 1:2 的比例
```

### 2.4 BIG_STRIDE 的选择

`BIG_STRIDE` 是一个常量，用于计算 pass 值（stride 增量）。

选择原则：
1. 必须足够大，能被常见的优先级值整除
2. 不能太大，避免 stride 溢出

我选择 `65536 = 2^16`，可以被 2-256 范围内的大部分数整除。

### 2.5 在哪里存储 stride 和 priority？

需要存储在进程控制块中，因为这是进程的调度属性：

```c
// os/proc.h
struct proc {
    // ... 原有字段 ...
    /* ch5: stride调度算法所需字段 */
    uint64 stride;    /* 当前已运行的"长度" */
    uint64 priority;  /* 进程优先级，默认16 */
};
```

**注意**：使用 `uint64` 类型，避免 stride 溢出问题。

### 2.6 修改调度器

原来的调度器使用简单的 FIFO 队列：

```c
struct thread *fetch_task() {
    int index = pop_queue(&task_queue);
    return id_to_task(index);
}
```

需要改为遍历队列，选择 stride 最小的任务：

```c
struct thread *fetch_task() {
    // 遍历队列，找到 stride 最小的任务
    // 从队列中移除该任务
    // 更新其 stride
    return 选中的任务;
}
```

## 三、代码实现

### 3.1 修改进程控制块

```c
// os/proc.h
struct proc {
    // ... 原有字段 ...
    /* ch5: stride调度算法所需字段 */
    uint64 stride;    /* 当前已运行的"长度" */
    uint64 priority;  /* 进程优先级，默认16 */
};
```

### 3.2 初始化 stride 和 priority

```c
// os/proc.c - allocproc()
struct proc *allocproc() {
    // ... 分配进程 ...

    /* ch5: 初始化stride调度相关字段 */
    p->stride = 0;
    p->priority = 16;  /* 默认优先级为16 */

    return p;
}
```

### 3.3 实现 sys_spawn

```c
// os/syscall.c

/* ch5: sys_spawn - 创建新进程并执行程序 */
int sys_spawn(uint64 path_va)
{
    struct proc *p = curr_proc();
    char path[MAX_STR_LEN];
    struct inode *ip;
    struct proc *np;
    int i;

    /* ch5: 从用户空间拷贝路径名 */
    if (copyinstr(p->pagetable, path, path_va, MAX_STR_LEN) < 0)
        return -1;

    /* ch5: 查找可执行文件 */
    if ((ip = namei(path)) == NULL) {
        errorf("spawn: invalid file name %s\n", path);
        return -1;
    }

    /* ch5: 分配新进程 */
    if ((np = allocproc()) == NULL) {
        iput(ip);
        return -1;
    }

    /* ch5: 设置父进程 */
    np->parent = p;

    /* ch5: 初始化标准IO */
    init_stdio(np);

    /* ch5: 复制父进程的文件描述符表(跳过stdio) */
    for (i = 3; i < FD_BUFFER_SIZE; i++) {
        if (p->files[i] != NULL) {
            p->files[i]->ref++;
            np->files[i] = p->files[i];
        }
    }

    /* ch5: 加载可执行文件 */
    bin_loader(ip, np);
    iput(ip);

    /* ch5: 设置命令行参数 */
    char *argv[2];
    argv[0] = path;
    argv[1] = NULL;
    struct thread *nt = &np->threads[0];
    nt->trapframe->a0 = push_argv(np, argv);

    /* ch5: 将新进程加入调度队列 */
    nt->state = RUNNABLE;
    add_task(nt);

    return np->pid;
}
```

### 3.4 实现 sys_set_priority

```c
// os/syscall.c

/* ch5: sys_set_priority - 设置进程优先级 */
int sys_set_priority(long long prio)
{
    /* ch5: 优先级必须 >= 2 */
    if (prio < 2)
        return -1;

    struct proc *p = curr_proc();
    p->priority = prio;
    return prio;
}
```

### 3.5 实现 Stride 调度

```c
// os/proc.c

/* ch5: BIG_STRIDE常量，用于计算pass值 */
#define BIG_STRIDE 65536

/* ch5: 从任务队列中获取stride最小的任务 */
struct thread *fetch_task()
{
    if (task_queue.empty)
        return NULL;

    /* ch5: 暴力扫描找stride最小的任务 */
    int min_idx = -1;
    uint64 min_stride = (uint64)-1;  /* 最大值 */
    int count = 0;

    /* ch5: 计算队列中的元素数量 */
    if (task_queue.front <= task_queue.tail && !task_queue.empty) {
        count = task_queue.tail - task_queue.front;
    } else {
        count = task_queue.size - task_queue.front + task_queue.tail;
    }
    if (task_queue.empty)
        count = 0;

    /* ch5: 遍历队列找stride最小的 */
    for (int i = 0; i < count; i++) {
        int idx = (task_queue.front + i) % task_queue.size;
        int task_id = task_queue.data[idx];
        struct thread *t = id_to_task(task_id);
        if (t == NULL || t->state != RUNNABLE)
            continue;

        uint64 stride = t->process->stride;
        if (stride < min_stride) {
            min_stride = stride;
            min_idx = idx;
        }
    }

    if (min_idx == -1) {
        /* ch5: 没找到可运行的任务，按原方式返回 */
        int index = pop_queue(&task_queue);
        return id_to_task(index);
    }

    /* ch5: 从队列中移除选中的任务 */
    int task_id = task_queue.data[min_idx];
    /* ch5: 将后面的元素前移 */
    for (int i = min_idx;
         i != (task_queue.tail - 1 + task_queue.size) % task_queue.size;
         i = (i + 1) % task_queue.size) {
        task_queue.data[i] = task_queue.data[(i + 1) % task_queue.size];
    }
    task_queue.tail = (task_queue.tail - 1 + task_queue.size) % task_queue.size;
    if (task_queue.front == task_queue.tail)
        task_queue.empty = 1;

    struct thread *t = id_to_task(task_id);
    if (t != NULL) {
        /* ch5: 更新stride: stride += BIG_STRIDE / priority */
        t->process->stride += BIG_STRIDE / t->process->priority;
    }

    return t;
}
```

### 3.6 添加系统调用入口

```c
// os/syscall.c - syscall() 函数的 switch 语句中
case SYS_spawn:
    ret = sys_spawn(args[0]);
    break;
case SYS_setpriority:
    ret = sys_set_priority(args[0]);
    break;
```

## 四、遇到的问题与解决

### 问题1：类型不存在

**现象**：编译报错 `unknown type name 'int64'`

**原因**：我在参数类型中使用了 `int64`，但系统没有定义这个类型。

**解决**：改用标准类型 `long long`：
```c
int sys_set_priority(long long prio)  // 而不是 int64 prio
```

### 问题2：队列操作复杂

**问题**：原来的队列只支持 FIFO 操作，但 stride 调度需要从任意位置移除元素。

**解决**：在 `fetch_task` 中实现：
1. 遍历队列找到 stride 最小的
2. 手动移除该元素（将后面的元素前移）

这不是最优雅的解决方案（O(n) 复杂度），但对于小规模进程数足够了。更好的方案是使用最小堆。

## 五、知识点总结

1. **spawn vs fork+exec**：spawn 更高效，因为不需要复制父进程的地址空间
2. **Stride 调度算法**：
   - 每个进程维护 stride（虚拟运行时间）
   - 选择 stride 最小的进程运行
   - pass = BIG_STRIDE / priority
   - 保证高优先级进程获得更多 CPU 时间
3. **优先级范围**：通常 >= 2，避免除零和负数问题
4. **进程状态**：RUNNABLE 表示就绪，可以被调度器选中

## 六、验证正确性

### 测试 spawn

运行 `ch5b_usertest`，它会测试：
- spawn 能否正确创建新进程
- 新进程是否正确执行目标程序
- 父子进程的关系是否正确

### 测试 stride 调度

运行 `ch5t_usertest`，它会：
1. 创建多个不同优先级的进程
2. 统计各进程实际获得的 CPU 时间
3. 验证是否符合优先级比例

## 七、文件修改清单

| 文件 | 修改内容 |
|------|----------|
| `os/proc.h` | 添加 `stride` 和 `priority` 字段 |
| `os/proc.c` | 初始化字段，实现 stride 调度的 `fetch_task()` |
| `os/syscall.c` | 实现 `sys_spawn()` 和 `sys_set_priority()` |

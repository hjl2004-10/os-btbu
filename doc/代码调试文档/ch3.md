# 第三章：系统调用追踪 (sys_trace)

## 一、任务目标

实现 `sys_trace` 系统调用，支持以下三种功能：
- **trace_request = 0**：读取用户空间指定地址的一个字节
- **trace_request = 1**：向用户空间指定地址写入一个字节
- **trace_request = 2**：返回指定系统调用号的调用次数

## 二、探索过程

### 2.1 理解问题：我们需要追踪什么？

在开始编码之前，我们需要思考几个问题：

1. **读写用户内存**：内核如何安全地访问用户空间的内存？
2. **统计系统调用**：调用次数应该存储在哪里？如何在每次系统调用时更新？

### 2.2 第一个挑战：用户空间地址转换

用户程序传递给内核的是**虚拟地址**，而内核需要访问的是**物理地址**。这就引出了一个关键问题：

> 如何将用户空间的虚拟地址转换为内核可访问的地址？

翻阅代码，我发现 `vm.c` 中有一个 `useraddr` 函数：

```c
// os/vm.c
uint64 useraddr(pagetable_t pagetable, uint64 va)
{
    uint64 page = walkaddr(pagetable, va);
    if (page == 0)
        return 0;
    return page | (va & 0xFFFULL);  // 物理页基址 + 页内偏移
}
```

这个函数做了两件事：
1. 通过 `walkaddr` 找到虚拟地址对应的物理页
2. 将物理页基址与页内偏移组合，得到完整的物理地址

### 2.3 第二个挑战：系统调用计数

系统调用计数需要**每个进程独立维护**。这意味着我们需要在进程控制块(PCB)中添加一个数组。

首先，我需要定义数组的大小。查看系统调用号的范围：

```c
// os/proc.h
#define MAX_SYSCALL_NUM (500)  /* ch3: 系统调用最大数量 */
```

然后在 `struct proc` 中添加计数数组：

```c
// os/proc.h
struct proc {
    // ... 其他字段 ...
    /* ch3: 系统调用计数数组 */
    int syscall_count[MAX_SYSCALL_NUM];
};
```

### 2.4 第三个挑战：何时更新计数？

计数必须在**每次系统调用处理时**更新。查看 `syscall()` 函数：

```c
// os/syscall.c
void syscall()
{
    struct trapframe *trapframe = curr_thread()->trapframe;
    int id = trapframe->a7;  // 系统调用号在 a7 寄存器

    // ... 系统调用分发 ...

    /* ch3: 统计系统调用次数 */
    struct proc *p = curr_proc();
    if (id >= 0 && id < MAX_SYSCALL_NUM) {
        p->syscall_count[id]++;
    }
}
```

**关键洞察**：RISC-V 的系统调用约定是将调用号放在 `a7` 寄存器中，参数放在 `a0-a5` 中。

## 三、代码实现

### 3.1 修改进程控制块

```c
// os/proc.h
#define MAX_SYSCALL_NUM (500)  /* ch3: 系统调用最大数量 */

struct proc {
    // ... 原有字段 ...
    /* ch3: 系统调用计数数组 */
    int syscall_count[MAX_SYSCALL_NUM];
};
```

### 3.2 初始化计数数组

在 `allocproc()` 中初始化：

```c
// os/proc.c
struct proc *allocproc()
{
    // ... 分配进程 ...

    /* ch3: 初始化系统调用计数 */
    memset(p->syscall_count, 0, sizeof(p->syscall_count));

    return p;
}
```

### 3.3 实现 sys_trace

```c
// os/syscall.c

/* ch3: 系统调用追踪 */
int sys_trace(int trace_request, uint64 id, uint8 data)
{
    struct proc *p = curr_proc();

    if (trace_request == 0) {
        /* 读操作：从用户地址读取一个字节 */
        uint8 *addr = (uint8 *)useraddr(p->pagetable, id);
        if (addr == NULL)
            return -1;
        return *addr;
    } else if (trace_request == 1) {
        /* 写操作：向用户地址写入一个字节 */
        uint8 *addr = (uint8 *)useraddr(p->pagetable, id);
        if (addr == NULL)
            return -1;
        *addr = data;
        return 0;
    } else if (trace_request == 2) {
        /* ch3: 返回指定syscall的调用次数 */
        if (id >= MAX_SYSCALL_NUM)
            return -1;
        return p->syscall_count[id];
    }
    return -1;
}
```

### 3.4 在 syscall 分发器中更新计数

```c
// os/syscall.c
void syscall()
{
    struct trapframe *trapframe = curr_thread()->trapframe;
    int id = trapframe->a7;

    // ... 系统调用处理 ...

    /* ch3: 统计系统调用次数 */
    struct proc *p = curr_proc();
    if (id >= 0 && id < MAX_SYSCALL_NUM) {
        p->syscall_count[id]++;
    }
}
```

### 3.5 添加系统调用入口

```c
// os/syscall.c - syscall() 函数的 switch 语句中
case SYS_trace:
    ret = sys_trace(args[0], args[1], args[2]);
    break;
```

## 四、遇到的问题与解决

### 问题1：读写权限检查不严格

**现象**：在 ch4 测试中，发现 `sys_trace` 的读写操作没有检查页面权限。

**分析**：`useraddr` 只检查地址是否有效，但没有检查权限位。我们需要：
- 读操作：检查 `PTE_R` 位
- 写操作：检查 `PTE_W` 位

**解决方案**（在 ch4 中完善）：

```c
/* ch4: 更新以检查虚存读写权限 */
int sys_trace(int trace_request, uint64 id, uint8 data)
{
    struct proc *p = curr_proc();
    if (trace_request == 0) {
        /* ch4: 读操作 - 检查地址是否用户可见且可读 */
        pte_t *pte = walk(p->pagetable, id, 0);
        if (pte == NULL || (*pte & PTE_V) == 0 ||
            (*pte & PTE_U) == 0 || (*pte & PTE_R) == 0)
            return -1;
        // ... 读取操作 ...
    } else if (trace_request == 1) {
        /* ch4: 写操作 - 检查地址是否用户可见且可写 */
        pte_t *pte = walk(p->pagetable, id, 0);
        if (pte == NULL || (*pte & PTE_V) == 0 ||
            (*pte & PTE_U) == 0 || (*pte & PTE_W) == 0)
            return -1;
        // ... 写入操作 ...
    }
    // ...
}
```

## 五、知识点总结

1. **地址空间隔离**：用户程序和内核使用不同的地址空间，内核访问用户内存需要地址转换
2. **页表权限位**：RISC-V 的 PTE 包含 V(有效)、R(可读)、W(可写)、X(可执行)、U(用户态可访问) 等权限位
3. **进程私有数据**：每个进程都有独立的 PCB，可以用来存储进程特有的状态（如系统调用计数）
4. **RISC-V 系统调用约定**：调用号在 `a7`，参数在 `a0-a5`，返回值在 `a0`

## 六、文件修改清单

| 文件 | 修改内容 |
|------|----------|
| `os/proc.h` | 添加 `MAX_SYSCALL_NUM` 宏和 `syscall_count` 数组 |
| `os/proc.c` | 在 `allocproc()` 中初始化计数数组 |
| `os/syscall.c` | 实现 `sys_trace()`，在 `syscall()` 中更新计数 |

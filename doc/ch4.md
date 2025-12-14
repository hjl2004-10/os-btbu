# 第四章：内存映射 (mmap/munmap)

## 一、任务目标

实现两个内存映射相关的系统调用：
- **sys_mmap**：将一段虚拟地址空间映射到物理内存（匿名映射）
- **sys_munmap**：取消已有的内存映射

## 二、探索过程

### 2.1 理解 mmap 的本质

在开始之前，我们需要理解 mmap 到底在做什么：

> mmap 的本质是**建立虚拟地址到物理地址的映射关系**。

用户程序请求："我想使用从地址 X 开始的 N 个字节的内存"

内核需要做的：
1. 检查这段地址是否可用（没有被其他映射占用）
2. 分配物理页面
3. 在页表中建立映射

### 2.2 第一个挑战：页表操作

要实现 mmap，我必须理解页表是如何工作的。

**RISC-V Sv39 页表结构**：
```
虚拟地址 (39位):
┌─────────┬─────────┬─────────┬──────────────┐
│ VPN[2]  │ VPN[1]  │ VPN[0]  │   Offset     │
│  9位    │  9位    │  9位    │    12位      │
└─────────┴─────────┴─────────┴──────────────┘
```

每级页表有 512 个条目（2^9），每个条目指向下一级页表或最终的物理页。

查看现有的页表操作函数：

```c
// os/vm.c

// 创建页表映射
int mappages(pagetable_t pagetable, uint64 va, uint64 size, uint64 pa, int perm);

// 查找虚拟地址对应的 PTE
pte_t *walk(pagetable_t pagetable, uint64 va, int alloc);

// 取消映射
void uvmunmap(pagetable_t pagetable, uint64 va, uint64 npages, int do_free);
```

### 2.3 第二个挑战：权限转换

mmap 的 `prot` 参数格式与页表的权限位格式不同：

| prot 位 | 含义 | PTE 权限位 |
|---------|------|-----------|
| bit 0   | 可读 | PTE_R (bit 1) |
| bit 1   | 可写 | PTE_W (bit 2) |
| bit 2   | 可执行 | PTE_X (bit 3) |

需要进行转换：

```c
int perm = PTE_U;  /* 用户可访问 */
if (prot & 0x1) perm |= PTE_R;
if (prot & 0x2) perm |= PTE_W;
if (prot & 0x4) perm |= PTE_X;
```

### 2.4 第三个挑战：地址冲突检测

在映射前，需要确保目标地址范围没有被占用：

```c
for (uint64 va = start; va < start + npages * PGSIZE; va += PGSIZE) {
    pte_t *pte = walk(p->pagetable, va, 0);  // 不创建新PTE
    if (pte != NULL && (*pte & PTE_V) != 0)
        return -1;  // 已被映射，返回错误
}
```

**关键洞察**：`walk` 函数的第三个参数 `alloc` 控制是否在 PTE 不存在时创建中间页表。检查时传入 0，避免创建不必要的页表结构。

### 2.5 第四个挑战：进程退出时的清理

这是我在 ch6 测试中遇到的一个 bug：

**现象**：运行 ch4_mmap0 测试时，进程退出后出现 `freewalk: leaf` panic。

**分析**：
1. mmap 可以在任意地址分配内存（只要没被占用）
2. 进程的 `max_page` 字段只记录程序代码和栈的范围
3. `uvmfree` 只清理 0 到 `max_page` 范围的页面
4. mmap 分配的高地址页面没有被清理，导致 `freewalk` 遇到"意外"的叶子页面

**解决方案**：修改 `freewalk` 函数，遇到叶子页面时释放它而不是 panic：

```c
// os/vm.c

/* ch4: 修改为同时释放叶子页面，支持mmap区域的清理 */
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
            /* ch4: 释放叶子页面的物理内存 */
            uint64 pa = PTE2PA(pte);
            kfree((void *)pa);
            pagetable[i] = 0;
        }
    }
    kfree((void *)pagetable);
}
```

## 三、代码实现

### 3.1 sys_mmap 实现

```c
// os/syscall.c

/* ch4: sys_mmap - 匿名内存映射 */
int sys_mmap(uint64 start, uint64 len, int prot, int flags)
{
    struct proc *p = curr_proc();

    /* ch4: 检查起始地址页对齐 */
    if (!PGALIGNED(start))
        return -1;

    /* ch4: 检查prot有效性：其他位必须为0 */
    if ((prot & ~0x7) != 0)
        return -1;

    /* ch4: 检查prot至少有一个权限(R/W/X) */
    if ((prot & 0x7) == 0)
        return -1;

    /* ch4: len为0时直接返回成功 */
    if (len == 0)
        return 0;

    /* ch4: 将len向上取整到页边界 */
    uint64 npages = (len + PGSIZE - 1) / PGSIZE;

    /* ch4: 检查[start, start+len)是否已被映射 */
    for (uint64 va = start; va < start + npages * PGSIZE; va += PGSIZE) {
        pte_t *pte = walk(p->pagetable, va, 0);
        if (pte != NULL && (*pte & PTE_V) != 0)
            return -1;  /* 已被映射 */
    }

    /* ch4: 将prot转换为PTE标志位 */
    int perm = PTE_U;  /* 用户可访问 */
    if (prot & 0x1) perm |= PTE_R;
    if (prot & 0x2) perm |= PTE_W;
    if (prot & 0x4) perm |= PTE_X;

    /* ch4: 逐页映射 */
    for (uint64 i = 0; i < npages; i++) {
        uint64 va = start + i * PGSIZE;
        char *mem = kalloc();
        if (mem == NULL)
            return -1;  /* 内存不足 */
        memset(mem, 0, PGSIZE);
        if (mappages(p->pagetable, va, PGSIZE, (uint64)mem, perm) != 0) {
            kfree(mem);
            return -1;
        }
    }

    return 0;
}
```

### 3.2 sys_munmap 实现

```c
// os/syscall.c

/* ch4: sys_munmap - 取消内存映射 */
int sys_munmap(uint64 start, uint64 len)
{
    struct proc *p = curr_proc();

    /* ch4: 检查起始地址页对齐 */
    if (!PGALIGNED(start))
        return -1;

    /* ch4: len为0时直接返回成功 */
    if (len == 0)
        return 0;

    /* ch4: 将len向上取整到页边界 */
    uint64 npages = (len + PGSIZE - 1) / PGSIZE;

    /* ch4: 检查[start, start+len)是否全部已映射 */
    for (uint64 va = start; va < start + npages * PGSIZE; va += PGSIZE) {
        pte_t *pte = walk(p->pagetable, va, 0);
        if (pte == NULL || (*pte & PTE_V) == 0)
            return -1;  /* 未映射 */
    }

    /* ch4: 取消映射并释放物理内存 */
    uvmunmap(p->pagetable, start, npages, 1);

    return 0;
}
```

### 3.3 添加系统调用入口

```c
// os/syscall.c - syscall() 函数的 switch 语句中
case SYS_mmap:
    ret = sys_mmap(args[0], args[1], args[2], args[3]);
    break;
case SYS_munmap:
    ret = sys_munmap(args[0], args[1]);
    break;
```

## 四、与 ch3 的关联：权限检查

在实现 mmap 的过程中，我发现 ch3 的 `sys_trace` 需要改进。

mmap 可以创建只读或只写的页面，但原来的 `sys_trace` 没有检查权限：

```c
// 原来的实现（有缺陷）
uint8 *addr = (uint8 *)useraddr(p->pagetable, id);
if (addr == NULL) return -1;
*addr = data;  // 如果页面只读，这里会出问题！
```

**改进后**：

```c
/* ch4: 写操作 - 检查地址是否用户可见且可写 */
pte_t *pte = walk(p->pagetable, id, 0);
if (pte == NULL || (*pte & PTE_V) == 0 ||
    (*pte & PTE_U) == 0 || (*pte & PTE_W) == 0)
    return -1;
```

## 五、知识点总结

1. **页表结构**：RISC-V Sv39 使用三级页表，每级 512 个条目
2. **PTE 格式**：包含物理页号和权限位（V/R/W/X/U等）
3. **内存分配**：`kalloc()` 分配物理页，`kfree()` 释放
4. **映射操作**：`mappages()` 建立映射，`uvmunmap()` 取消映射
5. **资源清理**：必须确保进程退出时释放所有分配的资源

## 六、调试技巧

### 如何确认映射是否成功？

在实现过程中，可以添加调试输出：

```c
tracef("mmap: va=%p, pa=%p, perm=%x", va, mem, perm);
```

### 常见错误

1. **忘记初始化内存**：`kalloc()` 返回的内存内容是未定义的，需要 `memset`
2. **页对齐问题**：地址必须是 PGSIZE (4096) 的倍数
3. **权限位转换错误**：prot 的位定义与 PTE 不同

## 七、文件修改清单

| 文件 | 修改内容 |
|------|----------|
| `os/syscall.c` | 实现 `sys_mmap()` 和 `sys_munmap()`，完善 `sys_trace()` 权限检查 |
| `os/vm.c` | 修改 `freewalk()` 以支持 mmap 区域的清理 |

# 第六章：硬链接 (Hard Links)

## 一、任务目标

实现文件系统的硬链接功能：

1. **sys_linkat**：创建硬链接（为现有文件创建新名称）
2. **sys_unlinkat**：删除硬链接（删除文件的一个名称）
3. **sys_fstat**：获取文件状态信息（包括链接计数）

## 二、探索过程

### 2.1 理解硬链接的本质

在深入代码之前，我需要理解硬链接是什么。

**文件系统的两层结构**：
```
目录项 (dirent)           inode
┌──────────────┐        ┌─────────────┐
│ name: "a.txt"│───┐    │ type: FILE  │
│ inum: 5      │   │    │ nlink: 2    │
└──────────────┘   ├───→│ size: 1024  │
┌──────────────┐   │    │ addrs[...]  │──→ 数据块
│ name: "b.txt"│───┘    └─────────────┘
│ inum: 5      │
└──────────────┘
```

**关键洞察**：
- 文件名（目录项）和文件内容（inode）是分离的
- 一个 inode 可以有多个名称（多个目录项指向它）
- `nlink` 记录有多少个名称指向这个 inode
- 只有当 `nlink = 0` 且没有进程打开该文件时，才真正删除数据

### 2.2 现有 inode 结构分析

查看现有的 inode 定义：

```c
// os/fs.h - 磁盘上的 inode
struct dinode {
    short type;     // 文件类型
    short pad[3];   // 填充
    uint size;      // 文件大小
    uint addrs[NDIRECT + 1];  // 数据块地址
};

// os/file.h - 内存中的 inode
struct inode {
    uint dev;       // 设备号
    uint inum;      // inode 编号
    int ref;        // 内存引用计数
    int valid;      // 是否从磁盘加载
    short type;     // 文件类型
    uint size;      // 文件大小
    uint addrs[NDIRECT + 1];
};
```

**问题发现**：两个结构都没有 `nlink` 字段！

### 2.3 第一步：添加 nlink 字段

需要修改两处：

**磁盘 inode（持久化存储）**：
```c
// os/fs.h
struct dinode {
    short type;
    short nlink;    /* ch6: 硬链接数量 */
    short pad[2];   // 减少一个 pad
    uint size;
    uint addrs[NDIRECT + 1];
};
```

**内存 inode（运行时缓存）**：
```c
// os/file.h
struct inode {
    uint dev;
    uint inum;
    int ref;
    int valid;
    short type;
    short nlink;    /* ch6: 硬链接数量 */
    uint size;
    uint addrs[NDIRECT + 1];
};
```

**警告**：修改 `dinode` 结构会改变磁盘布局！需要同步修改文件系统镜像生成工具。

### 2.4 第二步：同步修改 mkfs 工具

文件系统镜像是由 `nfs/fs.c` 生成的。它也有自己的 `dinode` 定义：

```c
// nfs/fs.h - 需要与 os/fs.h 保持一致
struct dinode {
    short type;
    short nlink;    /* ch6: 硬链接数量 */
    short pad[2];
    uint size;
    uint addrs[NDIRECT + 1];
};
```

同时，创建文件时要初始化 nlink：

```c
// nfs/fs.c - ialloc()
uint ialloc(ushort type)
{
    uint inum = freeinode++;
    struct dinode din;

    bzero(&din, sizeof(din));
    din.type = xshort(type);
    din.nlink = xshort(1);  /* ch6: 初始化链接计数为1 */
    din.size = xint(0);
    winode(inum, &din);
    return inum;
}
```

### 2.5 第三步：修改 fs.c 中的 inode 操作

需要在所有操作 inode 的地方处理 nlink：

**分配 inode 时**：
```c
// os/fs.c - ialloc()
dip->nlink = 1;  /* ch6: 初始化链接数为1 */
```

**从磁盘读取 inode 时**：
```c
// os/fs.c - ivalid()
ip->nlink = dip->nlink;  /* ch6: 读取硬链接数 */
```

**写回磁盘时**：
```c
// os/fs.c - iupdate()
dip->nlink = ip->nlink;  /* ch6: 更新硬链接数 */
```

**释放 inode 时**（关键！）：
```c
// os/fs.c - iput()
void iput(struct inode *ip)
{
    /* ch6: 当引用计数为1且nlink为0时，释放inode */
    if (ip->ref == 1 && ip->valid && ip->nlink == 0) {
        // 没有目录项指向这个 inode，且这是最后一个引用
        itrunc(ip);    // 释放数据块
        ip->type = 0;  // 标记为未使用
        iupdate(ip);   // 写回磁盘
        ip->valid = 0;
    }
    ip->ref--;
}
```

### 2.6 理解 linkat 的实现步骤

创建硬链接需要：
1. 找到源文件的 inode
2. 增加 nlink 计数
3. 在目录中添加新的目录项，指向同一个 inode

```
linkat("old.txt", "new.txt"):

目录:                      inode:
┌─────────────────┐       ┌─────────────┐
│ "old.txt" → 5   │       │ nlink: 1→2  │
│ "new.txt" → 5   │ ←新增 │ ...         │
└─────────────────┘       └─────────────┘
```

### 2.7 理解 unlinkat 的实现步骤

删除硬链接需要：
1. 找到文件的 inode
2. 从目录中删除目录项
3. 减少 nlink 计数
4. 如果 nlink 变为 0 且没有进程打开该文件，释放数据

```
unlinkat("new.txt"):

目录:                      inode:
┌─────────────────┐       ┌─────────────┐
│ "old.txt" → 5   │       │ nlink: 2→1  │
│ (删除 new.txt)  │       │ ...         │
└─────────────────┘       └─────────────┘
```

### 2.8 添加 dirunlink 函数

原来的文件系统只有 `dirlink`（添加目录项），没有删除功能。需要添加：

```c
// os/fs.c

/* ch6: 从目录中删除指定名称的目录项 */
int dirunlink(struct inode *dp, char *name)
{
    uint off;
    struct dirent de;
    struct inode *ip;

    /* ch6: 查找目录项 */
    if ((ip = dirlookup(dp, name, &off)) == 0)
        return -1;  /* 文件不存在 */

    iput(ip);

    /* ch6: 清空目录项 */
    memset(&de, 0, sizeof(de));
    if (writei(dp, 0, (uint64)&de, off, sizeof(de)) != sizeof(de))
        panic("dirunlink");

    return 0;
}
```

### 2.9 Stat 结构体设计

`fstat` 需要返回文件状态信息。需要定义用户空间可见的结构：

```c
// os/syscall.c

#define S_IFDIR 0x040000   /* directory */
#define S_IFREG 0x100000   /* regular file */

struct Stat {
    uint64 dev;     /* 设备号 */
    uint64 ino;     /* inode编号 */
    uint32 mode;    /* 文件类型 */
    uint32 nlink;   /* 硬链接数量 */
    uint64 pad[7];  /* 填充 */
};
```

## 三、代码实现

### 3.1 sys_linkat

```c
// os/syscall.c

/* ch6: sys_linkat - 创建硬链接 */
int sys_linkat(int olddirfd, uint64 oldpath_va, int newdirfd,
               uint64 newpath_va, uint flags)
{
    struct proc *p = curr_proc();
    char oldpath[MAX_STR_LEN], newpath[MAX_STR_LEN];
    struct inode *ip, *dp;

    /* ch6: 从用户空间拷贝路径 */
    if (copyinstr(p->pagetable, oldpath, oldpath_va, MAX_STR_LEN) < 0)
        return -1;
    if (copyinstr(p->pagetable, newpath, newpath_va, MAX_STR_LEN) < 0)
        return -1;

    /* ch6: 新旧路径相同则返回错误 */
    if (strncmp(oldpath, newpath, MAX_STR_LEN) == 0)
        return -1;

    /* ch6: 查找源文件 */
    if ((ip = namei(oldpath)) == NULL)
        return -1;
    ivalid(ip);

    /* ch6: 不能对目录创建硬链接 */
    if (ip->type == T_DIR) {
        iput(ip);
        return -1;
    }

    /* ch6: 增加链接计数 */
    ip->nlink++;
    iupdate(ip);

    /* ch6: 在根目录中添加新的目录项 */
    dp = root_dir();
    if (dirlink(dp, newpath, ip->inum) < 0) {
        /* ch6: 添加失败，恢复链接计数 */
        ip->nlink--;
        iupdate(ip);
        iput(ip);
        iput(dp);
        return -1;
    }

    iput(dp);
    iput(ip);
    return 0;
}
```

### 3.2 sys_unlinkat

```c
// os/syscall.c

/* ch6: sys_unlinkat - 删除硬链接 */
int sys_unlinkat(int dirfd, uint64 path_va, uint flags)
{
    struct proc *p = curr_proc();
    char path[MAX_STR_LEN];
    struct inode *ip, *dp;

    /* ch6: 从用户空间拷贝路径 */
    if (copyinstr(p->pagetable, path, path_va, MAX_STR_LEN) < 0)
        return -1;

    /* ch6: 查找文件 */
    if ((ip = namei(path)) == NULL)
        return -1;
    ivalid(ip);

    /* ch6: 不能删除目录（简化实现） */
    if (ip->type == T_DIR) {
        iput(ip);
        return -1;
    }

    /* ch6: 从根目录中删除目录项 */
    dp = root_dir();
    if (dirunlink(dp, path) < 0) {
        iput(ip);
        iput(dp);
        return -1;
    }
    iput(dp);

    /* ch6: 减少链接计数 */
    ip->nlink--;
    iupdate(ip);
    iput(ip);  /* ch6: 如果nlink为0，iput会释放inode */

    return 0;
}
```

### 3.3 sys_fstat

```c
// os/syscall.c

/* ch6: sys_fstat - 获取文件状态 */
int sys_fstat(int fd, uint64 st_va)
{
    struct proc *p = curr_proc();
    struct Stat st;

    /* ch6: 检查fd有效性 */
    if (fd < 0 || fd >= FD_BUFFER_SIZE)
        return -1;

    struct file *f = p->files[fd];
    if (f == NULL)
        return -1;

    /* ch6: 只支持inode类型的文件 */
    if (f->type != FD_INODE)
        return -1;

    struct inode *ip = f->ip;
    ivalid(ip);

    /* ch6: 填充Stat结构体 */
    memset(&st, 0, sizeof(st));
    st.dev = 0;  /* 设备号写死为0 */
    st.ino = ip->inum;
    st.mode = (ip->type == T_DIR) ? S_IFDIR : S_IFREG;
    st.nlink = ip->nlink;

    /* ch6: 拷贝到用户空间 */
    if (copyout(p->pagetable, st_va, (char *)&st, sizeof(st)) < 0)
        return -1;

    return 0;
}
```

## 四、遇到的问题与解决

### 问题1：ivalid: no type

**现象**：运行测试时 panic `ivalid: no type`

**原因**：旧的文件系统镜像是在修改 `dinode` 结构之前生成的。磁盘上的 inode 布局与代码不匹配。

**解决**：
1. 修改 `nfs/fs.h` 中的 `dinode` 定义
2. 修改 `nfs/fs.c` 中的 `ialloc()` 初始化 nlink
3. 重新生成文件系统镜像

### 问题2：freewalk: leaf（来自 ch4）

**现象**：ch6 测试中运行 ch4_mmap0 时 panic

**原因**：这是 ch4 的遗留问题，mmap 的页面没有被正确清理。

**解决**：见 ch4 文档，修改 `freewalk` 函数。

## 五、知识点总结

1. **硬链接 vs 软链接**：
   - 硬链接：多个目录项指向同一个 inode
   - 软链接：一个特殊文件，内容是另一个文件的路径

2. **nlink 语义**：
   - 创建文件时 nlink = 1
   - 创建硬链接时 nlink++
   - 删除链接时 nlink--
   - nlink = 0 且 ref = 0 时释放数据

3. **inode 的两个引用计数**：
   - `nlink`：磁盘上的目录项数（持久化）
   - `ref`：内存中的引用数（临时）

4. **数据一致性**：修改 nlink 后必须调用 `iupdate()` 写回磁盘

5. **错误处理**：操作失败时必须恢复已修改的状态（如 nlink）

## 六、测试用例分析

### ch6b_usertest 测试内容

1. 创建文件，检查 nlink = 1
2. 创建硬链接，检查 nlink = 2
3. 通过两个名称都能访问同一内容
4. 删除一个链接，检查 nlink = 1
5. 删除最后一个链接，文件被释放

### ch6_usertest 额外测试

1. 大量创建/删除链接
2. 与 mmap 的交互（ch4_mmap0）
3. 并发访问

## 七、文件修改清单

| 文件 | 修改内容 |
|------|----------|
| `os/fs.h` | 添加 `nlink` 到 `dinode`，声明 `dirunlink()` |
| `os/file.h` | 添加 `nlink` 到 `inode` |
| `os/fs.c` | 修改 `ialloc/ivalid/iupdate/iput`，添加 `dirunlink()` |
| `os/syscall.c` | 实现 `sys_linkat/sys_unlinkat/sys_fstat` |
| `nfs/fs.h` | 同步修改 `dinode` 结构 |
| `nfs/fs.c` | 修改 `ialloc()` 初始化 nlink |

# lab5

## 第五章：缺页异常与页面置换

本章内容：
- ELF格式程序加载
- 缺页异常处理
- 页面置换算法
- 虚拟内存管理进阶

---

## 1. fork系统调用 - 进程复制

### fork函数功能说明 (os/syscall.c)
**作用**：创建一个与当前进程几乎完全相同的子进程。这是Unix系统中创建新进程的基本方式，实现了进程的复制机制。

### 函数定义

```c
int fork()  // 函数名：创建子进程；无参数；返回值：父进程中返回子进程的PID，子进程中返回0，失败返回-1
{
    struct proc *p = curr_proc();  // 获取当前进程的指针（父进程），curr_proc()返回全局变量current_proc
    struct proc *np = allocproc();  // 调用allocproc分配一个新的进程控制块（PCB），np指向子进程的PCB

    // 步骤1：复制父进程的页表和用户内存空间
    // Copy user memory from parent to child.
    uvmcopy(p->pagetable, np->pagetable, p->max_page);  // 将父进程的用户地址空间完整复制到子进程
    // uvmcopy参数说明：
    //   - p->pagetable: 父进程的页表根地址
    //   - np->pagetable: 子进程的页表根地址（已由allocproc创建）
    //   - p->max_page: 父进程用户空间的最大页数（从BASE_ADDRESS开始的页面数量）
    // 复制内容包括：用户程序代码、数据、栈等所有用户空间页面

    // 步骤2：复制max_page信息
    np->max_page = p->max_page;  // 将父进程的max_page复制给子进程，确保子进程知道用户空间的大小
    // 用途：子进程退出时需要释放这些页面

    // 步骤3：复制父进程的陷阱帧（trapframe）
    // copy saved user registers.
    *(np->trapframe) = *(p->trapframe);  // 将父进程的trapframe结构完整复制到子进程
    // trapframe保存了进程的所有用户态寄存器状态，包括：
    //   - 通用寄存器：ra, sp, gp, tp, a0-a7, s0-s11, t0-t6
    //   - 特殊寄存器：epc（程序计数器）, kernel_satp, kernel_sp, kernel_trap等
    // 复制后，子进程的用户态寄存器状态与父进程完全相同

    // 步骤4：修改子进程的返回值
    // Cause fork to return 0 in the child.
    np->trapframe->a0 = 0;  // 将子进程的a0寄存器（x10）设置为0
    // a0寄存器用于函数返回值
    // 在RISC-V调用约定中，a0存放整数返回值
    // 这样当子进程从fork系统调用返回时，会得到返回值0
    // 而父进程的返回值（通过函数返回）是子进程的PID

    // 步骤5：建立父子关系
    np->parent = p;  // 将子进程的parent指针指向父进程
    // 用途：
    //   - 父进程可以wait()等待子进程结束
    //   - 子进程退出时需要通知父进程
    //   - 形成进程树结构

    // 步骤6：设置子进程为可运行状态
    np->state = RUNNABLE;  // 将子进程状态设置为RUNNABLE，表示可以被调度器选中运行

    // 步骤7：返回子进程PID（父进程执行路径）
    return np->pid;  // 返回子进程的进程ID（PID），父进程会得到这个返回值
    // 注意：子进程不会执行这条return语句，因为子进程从fork返回时a0=0
}
```

**工作原理**：

### fork的核心机制

**1. 写时复制（Copy-on-Write, COW）的简化实现**

ch5中的fork采用了**完全复制**策略（不是真正的COW）：
```c
uvmcopy(p->pagetable, np->pagetable, p->max_page);
```

**uvmcopy的工作流程**（vm.c:219）：
```c
int uvmcopy(pagetable_t old, pagetable_t new, uint64 max_page)
{
    // 遍历父进程的每一页用户空间
    for (i = 0; i < max_page * PAGE_SIZE; i += PGSIZE) {
        pte = walk(old, i, 0);  // 在父进程页表中查找第i页的PTE
        if (pte && (*pte & PTE_V)) {
            pa = PTE2PA(*pte);  // 获取父进程页面的物理地址
            flags = PTE_FLAGS(*pte);  // 获取页面的权限标志

            mem = kalloc();  // 分配一个新的物理页面
            memmove(mem, (char *)pa, PGSIZE);  // 复制整个页面（4KB）

            mappages(new, i, PGSIZE, (uint64)mem, flags);  // 在子进程页表中映射新页面
        }
    }
}
```

**复制内容示例**：
```
父进程地址空间：          子进程地址空间：
─────────────────         ─────────────────
0x10000 [用户程序]   →    0x10000 [用户程序副本]
0x12000 [数据段]     →    0x12000 [数据段副本]
0x13000 [用户栈]     →    0x13000 [用户栈副本]

每个页面都是独立的物理内存！
```

**2. 寄存器状态的完全复制**

```c
*(np->trapframe) = *(p->trapframe);
```

**trapframe结构包含的寄存器**：
```c
struct trapframe {
    uint64 kernel_satp;      // 内核页表
    uint64 kernel_sp;        // 内核栈指针
    uint64 kernel_trap;      // 内核trap处理函数地址
    uint64 epc;             // 程序计数器（用户态返回地址）
    uint64 kernel_hartid;   // CPU核心ID
    uint64 ra;              // x1 - 返回地址
    uint64 sp;              // x2 - 栈指针
    uint64 gp;              // x3 - 全局指针
    uint64 tp;              // x4 - 线程指针
    uint64 t0;              // x5
    uint64 t1;              // x6
    uint64 t2;              // x7
    uint64 s0;              // x8
    uint64 s1;              // x9
    uint64 a0;              // x10 - 函数参数/返回值 ← 关键！
    uint64 a1;              // x11 - 函数参数
    // ... a2-a7, s2-s11, t3-t6
};
```

**复制后，父子进程的寄存器状态**：
| 寄存器 | 父进程 | 子进程 | 说明 |
|--------|--------|--------|------|
| epc    | 0xXXXX | 0xXXXX | 相同：都从fork系统调用后的指令继续执行 |
| sp     | 0xYYYY | 0xYYYY | 相同：用户栈指针位置相同 |
| a0     | PID值  | **0**  | **不同**：子进程的a0被显式设置为0 |

**3. 返回值的差异实现**

```c
np->trapframe->a0 = 0;  // 子进程：a0 = 0
return np->pid;         // 父进程：返回子进程PID
```

**执行流程**：
```
1. 用户进程调用fork()
   ↓
2. 进入内核态（系统调用）
   ↓
3. 执行fork()函数：
   - 创建子进程PCB
   - 复制地址空间
   - 复制trapframe
   - 设置子进程a0=0
   - 返回子进程PID
   ↓
4. 父进程从系统调用返回：
   - a0 = 子进程PID
   - 继续执行fork()后的代码
   ↓
5. 子进程被调度器选中运行：
   - 从trapframe恢复寄存器
   - a0 = 0（之前设置的）
   - 从同一条指令继续执行
```

**为什么能从同一条指令返回两次？**
- **父进程路径**：`fork() → return np→pid → 系统调用返回 → 用户态a0=PID`
- **子进程路径**：`allocproc → 调度器 → usertrapret → 用户态a0=0`
- **关键**：两条路径的用户态返回地址（epc）相同，但a0不同

### fork后的进程状态

**进程树结构**：
```
init进程 (PID=1)
  ├─ shell进程 (PID=2)
  │    ├─ 编辑器 (PID=3)
  │    └─ 编译器 (PID=4)
  │         └─ 子进程 (PID=5)  ← fork()创建
  └─ ...其他进程
```

**父子进程关系**：
```c
np->parent = p;  // 子进程的parent指向父进程
```

**用途**：
1. **wait系统调用**：父进程等待子进程结束
2. **孤儿进程处理**：父进程先退出，子进程被init进程收养
3. **进程组管理**：信号、作业控制等

### fork的典型使用场景

**场景1：创建并发进程**
```c
int pid = fork();
if (pid == 0) {
    // 子进程代码
    printf("我是子进程，PID=%d\n", getpid());
    exit(0);
} else {
    // 父进程代码
    printf("我是父进程，子进程PID=%d\n", pid);
    wait(NULL);  // 等待子进程结束
}
```

**场景2：执行新程序（fork+exec）**
```c
int pid = fork();
if (pid == 0) {
    // 子进程
    exec("/bin/ls", argv);  // 替换为ls程序
    exit(1);
}
// 父进程继续运行
```

### fork的系统调用流程

**完整的调用链**：
```
用户程序
    ↓ (ecall指令)
uservec (trampoline.S)
    ↓
usertrap (trap.c)
    ↓
syscall (syscall.c) - 分发系统调用
    ↓
sys_fork (proc.c) - fork系统调用处理
    ↓
fork() - 创建子进程
    ↓
usertrapret - 返回父进程
    ↓
userret (trampoline.S)
    ↓
父进程返回用户态，a0=PID

（同时，子进程处于RUNNABLE状态）
    ↓
scheduler() - 调度器选择子进程
    ↓
usertrapret() - 首次切换到子进程
    ↓
子进程返回用户态，a0=0
```

### fork vs 现代操作系统的优化

**ch5的实现（完全复制）**：
- ✅ 简单直观
- ❌ 内存开销大：复制整个地址空间
- ❌ 性能低：如果子进程立即exec，复制是无用功

**现代操作系统的优化（写时复制 COW）**：
```
fork时：
- 父子进程共享相同的物理页面
- 页表项标记为只读
- 不真正复制数据

写操作时：
- 触发缺页异常
- 复制被修改的页面
- 其他页面仍然共享
```

**优势**：
- fork速度：从O(内存大小)降到O(1)
- exec效率：无需复制就替换
- 内存节省：只复制真正写入的页面

### 注意事项

1. **父进程先返回**：
   - fork()返回时，子进程可能还未运行
   - 父进程不要依赖子进程的执行顺序

2. **内存消耗**：
   - 完全复制意味着内存翻倍
   - 大程序fork可能导致内存不足

3. **文件描述符共享**：
   - fork后文件描述符表被复制
   - 但文件表项是共享的
   - 父子进程对同一文件的读写会相互影响

4. **异步执行**：
   - 子进程创建后立即与父进程并发运行
   - 执行顺序由调度器决定，不确定

### 常见错误

**错误1：忘记处理fork返回值**
```c
fork();  // 错误：不检查返回值
// 无法区分父进程和子进程
```

**错误2：假设执行顺序**
```c
int x = 1;
if (fork() == 0) {
    x = 2;
}
printf("%d\n", x);  // 可能打印1或2，顺序不确定
```

**错误3：内存泄漏**
```c
if (fork() == 0) {
    exit(0);
}
// 父进程忘记wait，子进程变成僵尸进程
```

### 与ch4 bin_loader的区别

| 特性 | ch4 bin_loader | ch5 fork |
|------|----------------|----------|
| 创建方式 | 从物理内存加载程序 | 复制当前进程 |
| 地址空间 | 新建 | 完全复制 |
| 初始状态 | 从BASE_ADDRESS开始 | 从fork位置继续 |
| 返回值 | 无 | 父:PID, 子:0 |
| 使用场景 | 系统启动、加载程序 | 运行时创建进程 |

---



---

## 2. wait系统调用 - 进程回收

### wait函数功能说明 (os/proc.c:205-234)

**作用**: 等待子进程结束,并回收子进程资源。如果不wait,子进程会变成僵尸进程。

### 函数定义与详细注释

```c
int wait(int pid, int *code)
{
    struct proc *np;
    int havekids;
    struct proc *p = curr_proc();  // 获取当前进程(父进程)

    for (;;) {
        // 步骤1: 扫描进程池,寻找符合条件的子进程
        havekids = 0;
        for (np = pool; np < &pool[NPROC]; np++) {
            // 检查条件:
            // 1. np->state != UNUSED    → 进程正在使用
            // 2. np->parent == p         → 是当前进程的子进程
            // 3. pid <= 0 || np->pid == pid → PID匹配(如果指定)
            if (np->state != UNUSED && np->parent == p && 
                (pid <= 0 || np->pid == pid)) {
                havekids = 1;
                
                // 步骤2: 检查子进程是否已退出(ZOMBIE状态)
                if (np->state == ZOMBIE) {
                    // 找到已退出的子进程,回收资源
                    np->state = UNUSED;    // 标记PCB为未使用
                    pid = np->pid;         // 记录子进程PID
                    *code = np->exit_code; // 获取退出码
                    return pid;            // 返回子进程PID
                }
            }
        }

        // 步骤3: 没有子进程
        if (!havekids) {
            return -1;  // 返回错误,没有可等待的子进程
        }

        // 步骤4: 有子进程但尚未退出,让出CPU
        p->state = RUNNABLE;
        sched();  // 切换到其他进程
        // 被唤醒后,继续循环检查
    }
}
```

### wait执行流程

```
父进程调用wait(child_pid, &status)
    ↓
扫描进程池:
    ├─ 找到child_pid对应的子进程
    │   └─ 子进程是ZOMBIE吗?
    │       ├─ 是 → 回收资源,返回退出码
    │       └─ 否 → 父进程睡眠,等待子进程退出
    │
    └─ 没找到child_pid
        └─ 返回错误(-1)

父进程睡眠后:
    ↓
调度器选择其他进程运行
    ↓
子进程执行exit(退出码)
    ├─ 释放地址空间
    ├─ 设置state=ZOMBIE
    └─ 唤醒等待的父进程
    ↓
父进程被唤醒,继续扫描
    ↓
发现子进程已是ZOMBIE
    ↓
回收资源,返回
```

### wait参数说明

```c
int wait(int pid, int *code)

// 参数:
//   pid: 要等待的子进程PID
//       - pid > 0: 等待指定PID的子进程
//       - pid <= 0: 等待任意子进程
//   code: 输出参数,存储子进程的退出码

// 返回值:
//   成功: 返回子进程的PID
//   失败: 返回-1(没有子进程)
```

**使用示例**:

```c
// 等待任意子进程
int status;
int child_pid = wait(-1, &status);

// 等待特定子进程
int status;
int child_pid = wait(1234, &status);

// 检查退出原因
if (WIFEXITED(status)) {
    printf("子进程正常退出,退出码: %d\n", WEXITSTATUS(status));
}
```

### 僵尸进程详解

**什么是僵尸进程?**

```
子进程exit()后的状态:
    ├─ 地址空间已释放 (freeproc)
    ├─ PCB仍存在 (state=ZOMBIE)
    ├─ 退出码保存在exit_code
    └─ 等待父进程wait()

如果父进程不wait():
    └─ 子进程永远是ZOMBIE状态
        └─ 占用PCB槽位
            └─ 资源泄漏
```

**僵尸进程示例**:

```c
// 创建僵尸进程的代码
int main() {
    if (fork() == 0) {
        exit(0);  // 子进程退出
    }
    // 父进程没有wait()
    sleep(100);  // 父进程继续运行
}
// 结果: 子进程变成僵尸进程,占用PCB
```

**查看僵尸进程**:
```bash
# 在Linux上查看
ps aux | grep Z
# 输出: user  1234  0.0  0.0  0  0  ?  Z  10:00  <defunct>
#                                    ↑
#                                 Z状态 = 僵尸进程
```

### 孤儿进程详解

**什么是孤儿进程?**

```
父进程先exit():
    └─ 子进程仍在运行
        └─ parent指针指向已退出的父进程
            └─ 子进程变成孤儿

操作系统处理:
    └─ 将孤儿进程的parent设为init进程(PID=1)
        └─ init进程负责回收孤儿进程
```

**孤儿进程示例**:

```c
// 创建孤儿进程的代码
int main() {
    if (fork() == 0) {
        sleep(100);  // 子进程长时间运行
        exit(0);
    }
    // 父进程立即退出
    exit(0);
}
// 结果: 子进程变成孤儿,被init进程收养
```

### wait阻塞与非阻塞

**阻塞式wait(当前实现)**:

```c
// 子进程未退出时,父进程阻塞
int pid = fork();
if (pid == 0) {
    sleep(10);  // 子进程睡眠10秒
    exit(0);
}
wait(NULL);  // 父进程阻塞等待10秒
```

**非阻塞式wait(需要额外支持)**:

```c
// WNOHANG选项(需要实现)
int status;
int ret = waitpid(-1, &status, WNOHANG);
if (ret == 0) {
    // 子进程尚未退出,立即返回
    printf("子进程还在运行\n");
}
```

### wait的典型使用模式

**模式1: 等待所有子进程**

```c
while (1) {
    int status;
    int pid = wait(-1, &status);
    if (pid == -1) {
        // 没有更多子进程
        break;
    }
    printf("子进程 %d 退出, 状态: %d\n", pid, status);
}
```

**模式2: 等待特定子进程**

```c
int target_pid = fork();
if (target_pid == 0) {
    // 子进程
    exit(42);
}

// 父进程等待特定子进程
int status;
int pid = wait(target_pid, &status);
if (pid == target_pid) {
    printf("子进程退出码: %d\n", status);
}
```

**模式3: 避免僵尸进程**

```c
// 信号处理方式(更先进)
void sigchld_handler(int sig) {
    // 当子进程退出时,内核发送SIGCHLD信号
    while (1) {
        int status;
        int pid = wait(-1, &status);
        if (pid == -1) break;
    }
}

int main() {
    signal(SIGCHLD, sigchld_handler);
    fork();
    // 父进程继续工作,不需要显式wait
    // 信号处理函数自动回收子进程
}
```

### wait与进程状态转换

```
正常退出流程:
子进程           父进程
  │
  │ exit(code)
  ↓
[ RUNNING ] 
  ↓
[ZOMBIE ] ←─────── wait() 扫描
  │                 ↓
  │              [ RUNNABLE ]
  │                 ↓
  │             [ 检查ZOMBIE ]
  │                 ↓
  │              [ 回收PCB ]
  │                 ↓
  │              [ 获取exit_code ]
  │                 ↓
  │             [ 返回给父进程 ]
  ↓
[ UNUSED ]
```

### wait的返回值处理

```c
int status;
int pid = wait(-1, &status);

if (pid == -1) {
    // 错误: 没有子进程
    perror("wait");
} else {
    // 成功: 子进程已退出
    printf("子进程 %d 退出\n", pid);
    
    // 解析退出状态
    if (WIFEXITED(status)) {
        // 正常退出
        printf("退出码: %d\n", WEXITSTATUS(status));
    } else if (WIFSIGNALED(status)) {
        // 被信号杀死
        printf("信号: %d\n", WTERMSIG(status));
    }
}
```

**宏定义**:
```c
// 检查是否正常退出
#define WIFEXITED(status)  (((status) & 0x7f) == 0)

// 获取退出码
#define WEXITSTATUS(status) (((status) >> 8) & 0xff)

// 检查是否被信号杀死
#define WIFSIGNALED(status) (!WIFEXITED(status))

// 获取信号编号
#define WTERMSIG(status) ((status) & 0x7f)
```

### wait在shell中的实现

```c
// shell等待命令完成
void run_command(char *cmd) {
    int pid = fork();
    if (pid == 0) {
        exec(cmd);
        exit(1);
    }
    
    // 父进程(shell)等待命令完成
    int status;
    wait(pid, &status);
    
    if (WIFEXITED(status)) {
        printf("命令退出码: %d\n", WEXITSTATUS(status));
    }
}
```

### 常见错误

**错误1: 忘记wait**

```c
for (int i = 0; i < 10; i++) {
    fork();
}
// 忘记wait任何子进程
// 结果: 10个僵尸进程!
```

**错误2: wait错误的子进程**

```c
int pid = fork();
if (pid == 0) {
    exit(0);
}
wait(9999, NULL);  // 错误: 等待不存在的子进程
// 正确做法: wait(pid, NULL)
```

**错误3: wait后不检查返回值**

```c
wait(NULL);  // 忽略返回值
// 无法知道子进程是否正常退出
```

### 关键要点总结

1. **wait阻塞等待**:
   - 子进程未退出时,父进程阻塞
   - 子进程退出后,父进程被唤醒
   - 返回子进程PID和退出码

2. **僵尸进程**:
   - exit()后PCB仍存在,等待wait()
   - 不wait会导致资源泄漏
   - 占用PCB槽位

3. **孤儿进程**:
   - 父进程先退出,子进程被init收养
   - init进程负责回收孤儿进程
   - 自动避免僵尸进程

4. **wait参数**:
   - pid > 0: 等待特定子进程
   - pid <= 0: 等待任意子进程
   - code: 输出退出码

5. **使用模式**:
   - 循环wait等待所有子进程
   - 信号处理自动回收
   - shell等待命令完成

---


---

## 3. exec系统调用 - 程序替换

### exec函数功能说明 (os/proc.c:193-203)

**作用**: 用新程序替换当前进程的地址空间,进程PID保持不变。这是Unix系统执行新程序的方式。

### 函数定义与详细注释

```c
int exec(char *name)
{
    // 步骤1: 根据文件名查找程序ID
    int id = get_id_by_name(name);
    // get_id_by_name做了什么?
    // - 在程序表中查找name对应的程序
    // - 返回程序的内部ID
    // - 如果找不到,返回-1
    
    if (id < 0)
        return -1;  // 文件不存在,exec失败

    struct proc *p = curr_proc();  // 获取当前进程

    // 步骤2: 释放旧的用户地址空间
    uvmunmap(p->pagetable, 0, p->max_page, 1);
    // uvmunmap做了什么?
    // - 遍历页表,从0到max_page
    // - 对每个页面:
    //   - 解映射页表项
    //   - 释放物理内存(因为do_free=1)
    // - 清空页表
    
    p->max_page = 0;  // 重置max_page
    // 此时进程的用户空间完全清空

    // 步骤3: 加载新程序
    loader(id, p);
    // loader做了什么?
    // - 从物理内存读取ELF格式的程序
    // - 解析ELF头部:
    //   - 获取程序入口点(_start地址)
    //   - 获取程序段信息
    // - 为代码段、数据段分配虚拟页面
    //   - 加载程序内容到内存
    //   - 设置用户栈
    //   - 初始化trapframe:
    //     - epc = 程序入口点
    //     - sp = 用户栈顶
    // - 更新p->max_page

    return 0;  // 成功返回0
    // 注意: exec成功后不会返回,因为程序已被替换
}
```

### exec执行流程

```
进程A执行exec("/bin/ls")
    ↓
进入内核态
    ↓
exec()函数执行:
    ├─ get_id_by_name("/bin/ls")  查找程序ID
    ├─ uvmunmap()                 释放旧地址空间
    │   └─ 原用户程序内存被释放
    ├─ max_page = 0                重置大小
    ├─ loader()                    加载新程序
    │   ├─ 解析ELF格式
    │   ├─ 分配新的用户空间
    │   ├─ 加载代码段、数据段
    │   ├─ 设置用户栈
    │   └─ 更新max_page
    └─ 返回0
    ↓
进程A返回用户态,执行新程序
    ↓
/bin/ls的开始地址 (_start)
    ↓
运行新程序
```

### exec对进程状态的影响

```
exec前:
┌────────────────────────────────┐
│ 进程A (PID=2)                  │
│ ├─ 用户空间: shell程序         │
│ │   ├─ 代码段                  │
│ │   ├─ 数据段                  │
│ │   └─ 栈                      │
│ ├─ PID = 2 (不变)             │
│ ├─ parent = init (不变)        │
│ └─ 打开的文件描述符 (保留)     │
└────────────────────────────────┘

exec("/bin/ls")后:
┌────────────────────────────────┐
│ 进程A (PID=2)                  │
│ ├─ 用户空间: ls程序 (完全替换) │
│ │   ├─ 代码段 (新)             │
│ │   ├─ 数据段 (新)             │
│ │   └─ 栈 (新)                │
│ ├─ PID = 2 (不变)             │
│ ├─ parent = init (不变)        │
│ └─ 打开的文件描述符 (保留)     │
└────────────────────────────────┘

注意: PID、父进程、文件描述符都保持不变
只有用户空间被完全替换
```

### exec vs fork+exec对比

**方式1: 仅exec (替换当前进程)**

```c
// shell进程直接exec
int main() {
    printf("执行ls...\n");
    exec("/bin/ls", ["ls", "-l", NULL]);
    printf("这行永远不会执行\n");
}
```

```
执行流程:
┌──────────────┐
│  shell进程   │
│   exec ls    │  ← shell进程被ls替换
└──────────────┘
      ↓
┌──────────────┐
│   ls进程     │  ← PID不变,但程序完全不同
│  (PID=2)     │
└──────────────┘
      ↓
ls执行完毕,进程退出
    ↓
shell不复存在!
```

**方式2: fork+exec (创建新进程)**

```c
// shell进程fork后再exec
int main() {
    int pid = fork();
    if (pid == 0) {
        // 子进程
        exec("/bin/ls", ["ls", "-l", NULL]);
    } else {
        // 父进程
        wait(NULL);
    }
    printf("继续运行shell\n");
}
```

```
执行流程:
┌──────────────┐       ┌──────────────┐
│  shell进程   │       │  子进程      │
│  (PID=2)     │       │  (PID=3)     │
│  fork()      │       │  exec ls     │
└──────────────┘       └──────────────┘
                               ↓
                        ┌──────────────┐
                        │   ls进程     │
                        │  (PID=3)     │
                        └──────────────┘
                               ↓
ls执行完毕,子进程退出
                               ↓
shell继续运行 (PID=2)
```

**为什么shell需要fork+exec?**

```
如果shell直接exec:
1. shell进程被ls替换
2. ls执行完毕后退出
3. shell不复存在
4. 无法继续接受用户命令!

使用fork+exec:
1. shell创建子进程
2. 子进程exec变成ls
3. ls执行完毕后退出
4. shell继续运行,等待下一个命令
5. 可以持续接受用户命令
```

### exec与ELF格式

**ELF文件结构**:

```
ELF文件布局:
┌──────────────┐
│ ELF Header   │  包含文件类型、入口点等
├──────────────┤
│ Program      │  程序头表
│  Header      │  描述如何加载到内存
│  Table       │
├──────────────┤
│ Code Section │  机器码 (.text)
├──────────────┤
│ Data Section │  数据 (.data, .bss)
└──────────────┘

loader的工作:
1. 读取ELF Header
2. 解析Program Header Table
3. 为每个可加载段分配内存
4. 将段内容加载到内存
5. 设置入口点地址
```

**Program Header示例**:

```c
struct prog_header {
    uint p_type;   // 段类型: PT_LOAD=可加载
    uint p_offset; // 段在文件中的偏移
    uint p_vaddr;  // 段的虚拟地址
    uint p_filesz; // 段在文件中的大小
    uint p_memsz;  // 段在内存中的大小
    uint p_flags;  // 权限: 读/写/执行
};

// 例如:
// p_type=PT_LOAD
// p_offset=0x1000
// p_vaddr=0x10000
// p_filesz=0x2000  (文件中8KB)
// p_memsz=0x3000   (内存中12KB,包括.bss)
// p_flags=PTE_R | PTE_X  (可读可执行)
```

### exec的返回值

**成功返回**:

```c
exec("/bin/ls", argv);
// 如果exec成功,永远不会返回
// 因为程序已被替换,从新程序的入口点开始执行
printf("这行不会执行\n");
```

**失败返回**:

```c
int ret = exec("/nonexistent", argv);
if (ret == -1) {
    // exec失败,返回-1
    // 原进程继续执行
    printf("exec失败: 文件不存在\n");
}
printf("程序继续运行\n");
```

**常见失败原因**:

1. **文件不存在**: `get_id_by_name()`返回-1
2. **不是可执行文件**: ELF格式错误
3. **内存不足**: `uvmalloc()`失败
4. **权限错误**: 文件不可执行

### exec与trapframe的关系

```c
// exec对trapframe的修改
loader(id, p) {
    // ...
    
    // 设置用户栈
    p->trapframe->sp = USER_STACK_TOP;
    
    // 设置程序入口点
    p->trapframe->epc = entry_point;
    // entry_point从ELF Header读取
    // 例如: 0x10000 (_start函数地址)
    
    // 清空用户寄存器
    memset(&p->trapframe->ra, 0, sizeof(p->trapframe) - 
                       offsetof(struct trapframe, ra));
}

// exec返回后,通过usertrapret返回用户态
// 从epc地址开始执行新程序
```

### exec的典型使用场景

**场景1: shell执行命令**

```c
// shell实现
void shell() {
    while (1) {
        char *cmd = read_command();
        
        int pid = fork();
        if (pid == 0) {
            // 子进程执行命令
            exec(cmd, args);
            exit(1);  // exec失败才执行到这里
        }
        
        // 父进程等待命令完成
        wait(NULL);
    }
}
```

**场景2: 程序自我升级**

```c
// 程序执行新版本
void upgrade_self() {
    printf("升级到新版本...\n");
    exec("/new/version", ["new_version", NULL]);
    // 永远不会返回
}
```

**场景3: 程序重启**

```c
void restart_program() {
    // 清理资源
    close_all_fds();
    
    // 重新执行自己
    exec("/path/to/program", ["program", NULL]);
}
```

### exec与环境变量

虽然ch5的exec实现比较简单,但完整的exec还支持环境变量:

```c
// 完整的exec原型
int execve(char *path, char *argv[], char *envp[]);

// 使用示例
char *argv[] = {"ls", "-l", NULL};
char *envp[] = {
    "PATH=/bin:/usr/bin",
    "HOME=/root",
    NULL
};
execve("/bin/ls", argv, envp);
```

**环境变量的传递**:

```
exec前:
进程的环境变量:
├─ PATH=/bin
├─ HOME=/user
└─ LANG=en

exec后:
新程序的环境变量:
├─ 继承父进程的环境
└─ 或者使用envp指定的环境
```

### exec与文件描述符

**exec对文件描述符的处理**:

```c
int main() {
    // 打开文件
    int fd = open("log.txt", O_WRONLY);
    
    // exec新程序
    exec("/bin/ls", ["ls", NULL]);
    
    // exec后,fd仍然有效!
    // 新程序可以继续使用这个fd
}
```

**关闭-on-exec标志**:

```c
int fd = open("file.txt", O_WRONLY);
int flags = fcntl(fd, F_GETFD);
fcntl(fd, F_SETFD, flags | FD_CLOEXEC);
// 设置FD_CLOEXEC后,exec会自动关闭这个fd

exec("/bin/ls", ["ls", NULL]);
// 执行后,fd已被关闭
```

### exec的优化: 直接跳转

**现代操作系统的优化**:

```c
// 如果exec失败,传统的做法:
exec("/program", argv);
exit(1);  // exec失败,退出进程

// 更高效的做法: 直接跳转
// 如果exec失败,直接返回用户态继续执行
// 避免额外的fork开销
```

### exec在系统调用链中的位置

```
用户程序: exec("/bin/ls")
    ↓
系统调用 (SYS_execve)
    ↓
syscall.c: sys_exec()
    ↓
proc.c: exec()
    ├─ get_id_by_name()  查找程序
    ├─ uvmunmap()        释放旧地址空间
    └─ loader()          加载新程序
        ├─ 解析ELF
        ├─ 分配内存
        └─ 设置trapframe
    ↓
返回用户态
    ↓
从新程序入口点开始执行 (_start)
```

### 关键要点总结

1. **exec替换地址空间**:
   - 完全释放旧的用户空间
   - 加载新程序的代码和数据
   - PID保持不变

2. **exec不创建新进程**:
   - 当前进程被替换
   - 没有新的PCB分配
   - 不需要父进程wait

3. **fork+exec的标准模式**:
   - shell使用fork+exec
   - fork创建子进程
   - 子进程exec执行新程序
   - 父进程wait等待

4. **exec成功不返回**:
   - 成功: 从新程序入口点执行
   - 失败: 返回-1,原进程继续

5. **exec与其他系统调用的关系**:
   - exec前打开的文件描述符保留
   - 环境变量可以传递
   - 信号处理函数需要重置

---


---

## 4. 进程调度机制

### scheduler函数 - 调度器实现 (os/proc.c:102-128)

调度器是操作系统的核心组件,负责决定哪个进程应该运行。让我们先理解注释掉的代码,然后再看当前实现。

#### 注释掉的代码 - 简单的轮询调度

```c
// os/proc.c:3-28 (注释部分)
void scheduler()
{
    struct proc *p;
    for (;;) {  // 无限循环,调度器永不返回
        /* 注释掉的代码: 简单轮询调度算法
        
        int has_proc = 0;  // 标记是否有可运行的进程
        
        // 遍历整个进程池
        for (p = pool; p < &pool[NPROC]; p++) {
            // 检查进程是否为RUNNABLE状态
            if (p->state == RUNNABLE) {
                has_proc = 1;  // 找到至少一个可运行进程
                
                // 打印调试信息
                tracef("swtich to proc %d", p - pool);
                
                // 设置进程为运行状态
                p->state = RUNNING;
                
                // 设置当前进程指针
                current_proc = p;
                
                // 上下文切换: 从idle切换到进程p
                swtch(&idle.context, &p->context);
            }
        }
        
        // 如果没有可运行进程,所有程序都结束了
        if(has_proc == 0) {
            panic("all app are over!\n");
        }
        */
    }
}
```

#### 注释代码详解

**1. 无限循环**

```c
for (;;) {
    // 调度器永不返回,一直循环
    // 每次循环选择一个进程运行
}
```

**为什么无限循环?**
- 调度器是操作系统的"主循环"
- 只要还有进程在运行,调度器就不断选择进程
- 只有当所有进程都结束时才panic

**2. 遍历进程池**

```c
for (p = pool; p < &pool[NPROC]; p++)
```

**进程池 (pool)**:
```c
struct proc pool[NPROC];  // 全局进程池
// NPROC: 进程池大小,通常512个
// pool: 进程数组的起始地址
// &pool[NPROC]: 数组结束地址

// 内存布局:
pool → ┌──────────────┐
       │ proc[0]      │
       ├──────────────┤
       │ proc[1]      │
       ├──────────────┤
       │ ...          │
       ├──────────────┤
       │ proc[511]    │
       └──────────────┘
       &pool[NPROC] →
```

**3. 状态检查**

```c
if (p->state == RUNNABLE) {
    // 只有RUNNABLE状态的进程可以被调度
}
```

**进程状态转换**:
```
UNUSED → RUNNABLE (allocproc)
   ↓
RUNNABLE → RUNNING (调度器选中)
   ↓
RUNNING → RUNNABLE (yield/时钟中断)
   ↓
RUNNING → ZOMBIE (exit)
   ↓
ZOMBIE → UNUSED (wait)
```

**4. 上下文切换**

```c
swtch(&idle.context, &p->context);
```

**swtch函数的作用**:
```c
// swtch.S (汇编实现)
void swtch(struct context *old, struct context *new)
```

**上下文切换过程**:
```
切换前:
┌─────────────┐
│ idle进程    │
│ context:    │
│  ra = ...   │
│  sp = ...   │
└─────────────┘
    current_proc = NULL

执行swtch(&idle.context, &p->context):
    ├─ 保存idle的寄存器到idle.context
    ├─ 从p->context恢复进程p的寄存器
    └─ 跳转到进程p的代码执行

切换后:
┌─────────────┐
│ 进程p       │
│ context:    │
│  ra = ...   │
│  sp = ...   │
└─────────────┘
    current_proc = p
```

**5. 检查进程池**

```c
if(has_proc == 0) {
    panic("all app are over!\n");
}
```

**为什么panic?**
- has_proc=0表示没有RUNNABLE进程
- 所有进程都已退出或UNUSED
- 系统无事可做,应该关机或halt
- 实验环境中直接panic

#### 注释代码的工作流程

```
调度器循环:
    ↓
遍历进程池pool[NPROC]:
    ├─ 查找RUNNABLE进程
    │
    ├─ 找到进程A (RUNNABLE)
    │   ├─ A.state = RUNNING
    │   ├─ current_proc = A
    │   ├─ swtch(&idle.context, &A.context)
    │   └─ 进程A开始运行
    │       │
    │       │ ... 进程A运行 ...
    │       │
    │       │ 进程A调用yield()
    │       │   ├─ A.state = RUNNABLE
    │       │   ├─ swtch(&A.context, &idle.context)
    │       │   └─ 回到调度器
    │       │
    ├─ 继续遍历
    │   │
    ├─ 找到进程B (RUNNABLE)
    │   └─ 切换到进程B
    │
    └─ 如果没有RUNNABLE进程
        └─ panic("all app are over!")
```

#### 注释代码的问题

**问题1: 效率低**
```
每次调度都遍历整个进程池(NPROC=512)
即使只有1个进程在运行,也要检查512个槽位
复杂度: O(NPROC)
```

**问题2: 缺乏调度策略**
```
按顺序遍历进程池:
- 总是优先选择槽位号小的进程
- pool[0] 总是比 pool[1] 先被调度
- 不支持优先级
- 不支持公平性
```

**问题3: 无法保证实时性**
```
假设:
- pool[0] 是CPU密集型进程
- pool[1] 是交互式进程

调度顺序:
- pool[0] → pool[0] → pool[0] → ...
- pool[1] 长时间得不到调度
- 交互式响应延迟高
```

#### 当前实现 - 使用调度队列

```c
// 当前代码(未注释)
void scheduler()
{
    struct proc *p;
    for (;;) {
        // 从调度队列获取下一个进程
        p = fetch_task();
        if (p == NULL) {
            panic("all app are over!\n");
        }
        
        tracef("swtich to proc %d", p - pool);
        p->state = RUNNING;
        current_proc = p;
        swtch(&idle.context, &p->context);
    }
}
```

**fetch_task()函数**:
```c
struct proc *fetch_task()
{
    int index = pop_queue(&task_queue);
    if (index < 0) {
        return NULL;  // 队列为空
    }
    return pool + index;
}
```

**调度队列优势**:
```
注释代码:
- 遍历pool[NPROC]
- 复杂度: O(512)

当前代码:
- pop_queue() O(1)
- 复杂度: O(1)
- 快512倍!
```

#### 两种调度算法对比

| 特性 | 注释代码(轮询) | 当前代码(队列) |
|------|----------------|----------------|
| 数据结构 | 数组pool[NPROC] | 队列task_queue |
| 查找复杂度 | O(NPROC) | O(1) |
| 调度策略 | 顺序遍历 | FIFO队列 |
| 公平性 | 低 | 中等 |
| 实现难度 | 简单 | 中等 |
| 可扩展性 | 差 | 好 |

#### 为什么注释掉旧代码?

**原因分析**:

1. **教学目的**:
   - 保留简单实现作为参考
   - 对比两种调度算法
   - 理解为什么需要队列

2. **性能优化**:
   - 旧代码效率低
   - 新代码使用队列
   - 适合实验需求

3. **代码演进**:
   - 先实现简单版本(注释代码)
   - 再优化为高效版本(当前代码)
   - 保留学习路径

#### 从注释代码到当前代码的改进

**改进1: 引入调度队列**

```c
// 旧代码: 遍历数组
for (p = pool; p < &pool[NPROC]; p++) {
    if (p->state == RUNNABLE) {
        // 找到进程
        break;
    }
}

// 新代码: 队列
p = fetch_task();  // O(1)
```

**改进2: 显式队列管理**

```c
// 进程主动加入队列
void yield() {
    current_proc->state = RUNNABLE;
    add_task(current_proc);  // 加入调度队列
    sched();
}
```

**改进3: 调度时机**

```c
// 时钟中断
void timer_interrupt() {
    set_next_timer();
    yield();  // 当前进程加入队列
    // 调度器选择下一个进程
}
```

#### 调度器的工作原理总结

**核心思想**:
```
调度器 = 操作系统的"交通指挥"

职责:
1. 选择哪个进程运行
2. 切换上下文
3. 确保所有进程公平执行

实现方式:
1. 遍历法(注释代码): 简单但低效
2. 队列法(当前代码): 高效且灵活
```

**关键点**:
1. **无限循环**: 调度器永不返回
2. **状态检查**: 只调度RUNNABLE进程
3. **上下文切换**: 使用swtch切换
4. **空转检查**: 无进程时panic

---


---

## 5. 系统启动与第一个进程

### load_init_app - 加载init进程 (os/loader.c)

**作用**: 系统启动时加载并运行第一个用户进程(init进程),这是用户空间的起点。

#### 函数定义与详细注释

```c
// os/loader.c
int load_init_app()
{
    // 步骤1: 查找init程序的ID
    int id = get_id_by_name(INIT_PROC);
    // INIT_PROC: init程序的名称,通常是"_initproc"
    // get_id_by_name: 在程序表中查找程序
    // 返回: 程序的内部ID,失败返回-1
    
    if (id < 0)
        panic("Cannot find INIT_PROC %s", INIT_PROC);
        // 找不到init程序是致命错误
        // 没有init进程,系统无法运行
    
    // 步骤2: 分配进程控制块(PCB)
    struct proc *p = allocproc();
    // allocproc做了什么?
    // - 从进程池pool[NPROC]中找一个UNUSED槽位
    // - 分配PID(递增的整数)
    // - 创建用户页表
    // - 分配内核栈
    // - 初始化context和trapframe
    
    if (p == NULL) {
        panic("allocproc\n");
        // 分配失败,通常是进程池满了
    }
    
    // 步骤3: 加载init程序
    debugf("load init proc %s", INIT_PROC);
    loader(id, p);
    // loader做了什么?
    // - 读取ELF格式的init程序
    // - 解析程序段信息
    // - 为代码段、数据段分配虚拟页面
    // - 加载程序内容到内存
    // - 设置用户栈
    // - 设置trapframe:
    //   - epc = 程序入口点
    //   - sp = 用户栈顶
    // - 设置进程为RUNNABLE状态
    
    return 0;  // 成功返回
}
```

#### load_init_app执行流程

```
系统启动
    ↓
main()函数 (os/main.c)
    ├─ 初始化硬件
    ├─ 初始化内存管理
    ├─ 初始化进程管理
    └─ load_init_app() ←───────┐
        ↓                     │
    get_id_by_name(INIT_PROC)  │
        ↓                     │
    找到init程序ID             │
        ↓                     │
    allocproc()                │
        ├─ 分配PCB             │
        ├─ 分配PID=1           │
        └─ 创建页表            │
        ↓                     │
    loader(id, p)              │
        ├─ 加载程序            │
        ├─ 设置内存            │
        └─ 设置trapframe       │
        ↓                     │
    将进程加入调度队列         │
        ↓                     │
    scheduler()                │
        ↓                     │
    init进程开始执行 ──────────┘
        ↓
    执行main()
        ↓
    fork()创建子进程
        ↓
    shell进程
```

#### init进程的特殊性

**PID=1**:
```c
// init进程是第一个用户进程
// 它的PID通常是1

// 其他进程都通过fork创建
// PID从2开始递增
```

**父进程的特殊**:
```c
// init进程的父进程是内核(虚拟的)
// parent指针可能为NULL

// 当其他进程变成孤儿时:
// 它们的parent设为NULL
// 实际上被init进程"收养"
```

**作用**:
```
init进程的职责:
1. 作为所有孤儿进程的父进程
2. 回收僵尸进程
3. 启动系统服务(如shell)
4. 维护系统运行
```

#### allocproc - 分配进程控制块

```c
// os/proc.c
struct proc *allocproc()
{
    struct proc *p;
    
    // 步骤1: 在进程池中找UNUSED槽位
    for (p = pool; p < &pool[NPROC]; p++) {
        if (p->state == UNUSED) {
            goto found;
        }
    }
    return 0;  // 没有可用槽位
    
found:
    // 步骤2: 初始化进程
    p->pid = allocpid();     // 分配PID
    p->state = USED;          // 标记为已使用
    
    // 步骤3: 创建用户页表
    p->pagetable = uvmcreate(p->trapframe);
    // uvmcreate:
    // - 分配页表根节点
    // - 映射trampoline页面
    // - 映射trapframe页面
    
    // 步骤4: 分配内核栈
    p->kstack = (uint64)kstack[p - pool];
    
    // 步骤5: 初始化context
    memset(&p->context, 0, sizeof(p->context));
    p->context.ra = (uint64)usertrapret;
    p->context.sp = p->kstack + KSTACK_SIZE;
    
    // 步骤6: 初始化trapframe
    memset(p->trapframe, 0, TRAP_PAGE_SIZE);
    
    return p;
}
```

**进程创建时的状态**:

```
allocproc()之后:
┌─────────────────────┐
│ 进程p               │
│ ├─ pid = 1          │  ← 分配的PID
│ ├─ state = USED     │  ← 初始状态
│ ├─ pagetable = xxx  │  ← 用户页表(空)
│ ├─ kstack = xxx     │  ← 内核栈
│ ├─ trapframe = xxx  │  ← 陷阱帧(清空)
│ └─ context = xxx    │  ← 上下文
└─────────────────────┘

loader()之后:
┌─────────────────────┐
│ 进程p               │
│ ├─ 用户空间已加载  │
│ ├─ trapframe已设置  │
│ │   ├─ epc = 0x10000
│ │   └─ sp = 0x7fffff
│ └─ state = RUNNABLE  │  ← 可以运行了
└─────────────────────┘
```

#### loader - 加载程序

```c
// os/loader.c
void loader(int id, struct proc *p)
{
    // 步骤1: 获取程序信息
    // 程序已在编译时嵌入到磁盘镜像中
    // 通过id定位程序位置
    
    // 步骤2: 解析ELF格式
    // 读取ELF Header
    // 获取程序入口点
    // 获取程序段信息
    
    // 步骤3: 加载代码段
    // 为代码段分配虚拟页面
    // 加载代码到内存
    // 设置为只读和可执行
    
    // 步骤4: 加载数据段
    // 分配虚拟页面
    // 加载数据到内存
    // 设置为可读写
    
    // 步骤5: 设置用户栈
    // 分配栈空间
    // 设置栈指针
    
    // 步骤6: 设置trapframe
    p->trapframe->epc = entry_point;  // 程序入口点
    p->trapframe->sp = USER_STACK_TOP; // 栈顶
    
    // 步骤7: 更新max_page
    p->max_page = 计算出的最大页数;
    
    // 步骤8: 设置进程为可运行
    p->state = RUNNABLE;
}
```

#### ELF格式解析

**ELF文件结构**:

```
init程序的ELF文件:
┌──────────────┐
│ ELF Header   │
│ - e_type     │
│ - e_entry    │ ← 程序入口点
└──────────────┘
┌──────────────┐
│ Program      │
│ Header Table  │
│ - p_type     │
│ - p_vaddr    │ ← 虚拟地址
│ - p_offset   │ ← 文件偏移
│ - p_filesz   │ ← 文件大小
└──────────────┘
┌──────────────┐
│ Code Section │ ← .text段
│ (机器码)     │
└──────────────┘
┌──────────────┐
│ Data Section │ ← .data段
│ (数据)       │
└──────────────┘
```

**加载过程**:

```
1. 读取ELF Header
   entry_point = elf_header.e_entry
   例如: 0x10000

2. 遍历Program Header Table
   for each program header:
       if (p_type == PT_LOAD):
           // 这是一个需要加载的段
           
           // 分配虚拟页面
           uvmalloc(p->pagetable, 
                    p_vaddr, 
                    p_memsz, 
                    PTE_W | PTE_R)
           
           // 加载内容到内存
           memmove((char*)p_vaddr, 
                  file + p_offset, 
                  p_filesz)

3. 设置trapframe
   p->trapframe->epc = entry_point
   p->trapframe->sp = USER_STACK_TOP

4. 更新进程信息
   p->max_page = 计算最大页数
   p->state = RUNNABLE
```

#### 用户空间布局

```
init进程的地址空间:

0x10000 ┌─────────────┐
        │ 代码段       │ ← 程序入口点
        │ .text        │   (_start)
        ├─────────────┤
        │ 数据段       │
        │ .data        │
        │ .bss         │
        ├─────────────┤
        │ 堆           │ ↑
        │              │ │
        │              │ │ 堆增长
        │              │ │
        ├─────────────┤ │
        │   栈         │ ↓
        │              │ ← USER_STACK_TOP
0x7FFFFF└─────────────┘

TRAMPOLINE (固定地址)
        │ trampoline.S │
        └─────────────┘

TRAPFRAME (固定地址)
        │ trapframe     │
        └─────────────┘
```

#### 从内核到用户的切换

```
1. load_init_app()完成
   ├─ allocproc(): 分配PCB
   ├─ loader(): 加载程序
   └─ 进程状态: RUNNABLE

2. scheduler()被调用
   ├─ fetch_task(): 获取init进程
   ├─ p->state = RUNNING
   └─ swtch(&idle.context, &p->context)

3. 首次进入用户空间
   ├─ usertrapret()
   ├─ 设置:
   │   ├─ sepc = entry_point
   │   ├─ sstatus.SPP = 0 (用户态)
   │   └─ sstatus.SPIE = 1 (开中断)
   ├─ 切换页表: 内核页表 → 用户页表
   └─ sret指令

4. 用户空间开始执行
   └─ 从entry_point开始
       └─ _start函数 (C运行时)
           └─ main()
```

#### init程序的实现

```c
// user/_initproc.c
int main() {
    printf("init process running...\n");
    
    // 创建子进程
    int pid = fork();
    if (pid == 0) {
        // 子进程
        exec("/sh", ["sh", NULL);
        exit(1);
    }
    
    // init进程: 父进程
    // 等待子进程
    wait(NULL);
    
    printf("shell exited, init exiting...\n");
    exit(0);
}
```

**init进程的作用**:

```
init进程的主要任务:
1. 作为所有孤儿进程的父进程
2. 启动shell或其他系统服务
3. 回收僵尸进程
4. 维护系统运行

为什么需要init?
- 提供一个稳定的父进程
- 自动回收孤儿进程
- 避免进程泄漏
```

#### 系统启动完整流程

```
1. 电源启动
   ↓
2. RustSBI (Bootloader)
   ↓
3. 内核入口 (os/main.c)
   ├─ 初始化硬件
   ├─ 初始化内存
   ├─ 初始化进程管理
   └─ load_init_app()
       ↓
4. 加载init进程
   ├─ allocproc() (PID=1)
   ├─ loader()
   └─ 加入调度队列
       ↓
5. 开始调度
   ├─ scheduler()
   └─ 切换到init进程
       ↓
6. init进程运行
   ├─ 执行main()
   ├─ fork()创建shell进程
   └─ wait()等待
       ↓
7. shell运行
   ├─ 显示提示符
   ├─ 读取命令
   └─ 执行命令
```

#### 关键要点总结

1. **load_init_app的作用**:
   - 系统启动时的第一个用户进程
   - 从内核空间过渡到用户空间
   - init进程PID=1

2. **加载过程**:
   - get_id_by_name: 查找程序
   - allocproc: 分配PCB
   - loader: 加载程序到内存

3. **ELF格式解析**:
   - 读取ELF Header
   - 解析Program Header
   - 加载代码段和数据段

4. **init进程的特殊性**:
   - 第一个用户进程
   - 所有孤儿进程的父进程
   - 负责回收僵尸进程

5. **从内核到用户的切换**:
   - loader设置trapframe
   - scheduler调度进程
   - usertrapret切换到用户空间
   - 从entry_point开始执行

6. **系统启动流程**:
   - Bootloader → 内核 → init → shell
   - 从特权级到用户空间的转变
   - 建立用户空间的执行环境

---


## 6. 用户Shell实现 (User Shell)

### usershell.c - 简易Shell实现 (user/src/usershell.c)

**作用**: 实现一个简单的命令行shell,提供用户交互界面,读取并执行用户命令。

#### 完整代码与逐行注释

```c
// user/src/usershell.c
#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/syscall.h"
#include "user/user.h"

// 栈式字符缓冲区,用于存储用户输入的命令
char buffer[128];  // 缓冲区大小128字节
int buffer_index = 0;  // 当前缓冲区位置(类似栈指针)

int main(int argc, char *argv[])
{
    // 步骤1: 显示欢迎信息
    printf("Simple Shell\n");
    printf("Type commands and press Enter\n");

    // 步骤2: 主循环 - 持续读取并执行命令
    while (1) {
        // 显示提示符
        printf("> ");

        // 步骤3: 读取一行输入(字符逐个读取)
        buffer_index = 0;  // 重置缓冲区位置

        while (1) {
            // 读取一个字符
            // getchar()是系统调用,从标准输入读取
            char ch = getchar();

            // 处理退格键 (ASCII 8 = Backspace)
            if (ch == 8) {
                if (buffer_index > 0) {
                    // 如果缓冲区不为空
                    buffer_index--;  // 弹出栈顶字符
                    printf("\b \b");  // 输出退格序列: 光标回退、空格擦除、再回退
                    // 视觉上删除了最后一个字符
                }
                // 如果buffer_index=0,忽略退格键
                continue;
            }

            // 处理回车键 (ASCII 13 = Carriage Return)
            // 在Windows系统上,行结束是\r\n
            if (ch == 13) {
                // 忽略\r,等待\n
                continue;
            }

            // 处理换行符 (ASCII 10 = Line Feed / Newline)
            if (ch == 10) {
                // 用户按下了Enter键
                buffer[buffer_index] = '\0';  // 在字符串末尾添加null终止符
                printf("\n");  // 换行,美化输出
                break;  // 退出输入循环
            }

            // 普通字符: 存入缓冲区
            if (buffer_index < 127) {  // 留一个位置给'\0'
                buffer[buffer_index] = ch;  // 压入字符到栈
                buffer_index++;  // 栈指针递增
                printf("%c", ch);  // 回显字符(让用户看到输入)
            }
            // 如果缓冲区满了,忽略多余字符
        }

        // 步骤4: 空命令检查
        if (buffer_index == 0) {
            // 用户只按了Enter,没有输入命令
            continue;  // 回到提示符
        }

        // 步骤5: 创建子进程执行命令
        int pid = fork();
        // fork()创建一个与当前进程相同的子进程
        // 返回值: 父进程中得到子进程PID,子进程中得到0

        if (pid == 0) {
            // ========== 子进程代码 ==========

            // 准备exec参数
            // exec需要的参数格式: char* argv[]
            // argv[0] = 程序名
            // argv[1] = 命令参数
            // argv[2] = NULL (表示参数结束)

            char *argv[2];
            argv[0] = buffer;  // 命令名称
            argv[1] = 0;       // NULL终止符

            // 执行命令
            // exec会用新程序替换当前进程的地址空间
            // 如果exec成功,永远不会返回
            exec(buffer, argv);

            // 如果exec失败,才会执行到这里
            // 例如: 用户输入了不存在的命令
            printf("exec failed: command not found\n");
            exit(1);  // 子进程退出

        } else {
            // ========== 父进程代码 ==========

            // 等待子进程结束
            // wait会阻塞,直到子进程调用exit()
            int exit_code;
            wait(pid, &exit_code);

            // 子进程已结束,shell继续运行
            // 回到循环开头,显示下一个提示符
        }
    }

    // 永远不会执行到这里
    return 0;
}
```

#### Shell的工作原理

**1. 交互式输入**

```
用户输入: ls -l

键盘事件序列:
├─ 'l' 字符 → 压入buffer[0], 回显'l'
├─ 's' 字符 → 压入buffer[1], 回显's'
├─ ' ' 空格 → 压入buffer[2], 回显' '
├─ '-' 字符 → 压入buffer[3], 回显'-'
├─ 'l' 字符 → 压入buffer[4], 回显'l'
├─ Backspace → buffer_index--, 从4减到3
│              视觉删除: 'l'消失
├─ 'l' 字符 → 压入buffer[4], 回显'l'
└─ Enter → buffer[5]='\0', 退出输入循环

buffer内容: "ls -l\0"
```

**2. 命令执行流程**

```
Shell进程
    │
    │ fork()
    ↓
┌───────┴───────┐
│               │
Shell子进程     Shell父进程
│               │
│ exec("/ls")   │ wait()
│   ↓           │   ↓
│ 替换为ls      │ 阻塞等待
│   ↓           │   ↓
│ 执行ls命令    │ 子进程结束
│   ↓           │   ↓
│ exit(0)       │ 返回
│               │
└───────┬───────┘
        │
    回到Shell主循环
        ↓
    显示下一个提示符
```

**3. 进程树结构**

```
init进程 (PID=1)
  │
  └─ shell进程 (PID=2)
       │
       ├─ ls子进程 (PID=3) ← 执行ls命令
       │    执行完后exit()
       │
       ├─ cat子进程 (PID=4) ← 执行cat命令
       │    执行完后exit()
       │
       └─ shell子进程 (PID=5) ← 用户可以启动子shell
            │
            └─ ...更多子进程
```

#### Shell的关键机制

**机制1: 逐字符输入处理**

```c
// 为什么不用scanf或gets?
// 因为需要实时处理退格键

while (1) {
    char ch = getchar();  // 读取一个字符

    if (ch == 8) {  // Backspace
        // 立即响应退格
        buffer_index--;
        printf("\b \b");
    }

    // 其他字符立即回显
    printf("%c", ch);
}
```

**退格键的实现**:
```c
// ASCII 8 = Backspace
// 控制序列: \b (回退) + 空格 + \b (再回退)

示例: 用户输入"abc",然后按退格

buffer: ['a', 'b', 'c', ...]
         ↑
     buffer_index=3

用户按Backspace:
  ├─ buffer_index-- (变为2)
  ├─ buffer变为: ['a', 'b', '\0', ...]
  └─ 输出"\b \b":
      ├─ \b: 光标回退到'c'下方
      ├─ 空格: 覆盖'c'为空格
      └─ \b: 光标再回退到空格下方
          结果: "ab|"
```

**机制2: fork+exec模式**

```c
int pid = fork();
if (pid == 0) {
    // 子进程: 执行命令
    exec(buffer, argv);
    exit(1);
} else {
    // 父进程: 等待命令完成
    wait(pid, &exit_code);
}
```

**为什么需要fork+exec?**

```
如果shell直接exec:
  ├─ shell进程被ls替换
  ├─ ls执行完毕
  └─ shell消失,用户无法继续输入命令!

使用fork+exec:
  ├─ shell创建子进程
  ├─ 子进程exec变成ls
  ├─ ls执行完毕,子进程exit
  └─ shell仍然存在,继续等待命令
```

#### 关键要点总结

1. **Shell的核心功能**:
   - 读取用户输入
   - 解析命令
   - 创建子进程(fork)
   - 执行程序(exec)
   - 等待完成(wait)
   - 循环执行

2. **逐字符输入处理**:
   - getchar()逐个读取
   - 实时回显
   - 处理退格键
   - 处理Enter键

3. **fork+exec模式**:
   - fork创建子进程
   - 子进程exec执行命令
   - 父进程wait等待
   - Shell保持运行

4. **Shell与内核的关系**:
   - Shell是用户程序
   - 通过系统调用与内核交互
   - 不是内核的一部分

---

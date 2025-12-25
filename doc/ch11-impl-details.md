# Ch11 实现细节文档

## 阶段一：四线程框架实现

### 完成时间
2025-12-25

### 文件变更

| 文件 | 操作 | 说明 |
|------|------|------|
| `user/src/ch11_npc.c` | 新增 | NPC四线程主程序 |
| `user/src/ch11_world.c` | 新增 | 世界管理器 |
| `user/Makefile` | 修改 | 添加ch11编译配置 |

### 实现细节

#### 1. 共享数据结构 (`struct npc_shared`)

```c
struct npc_shared {
    /* 同步原语 */
    int mutex_id;           /* 互斥锁ID */
    int cond_id;            /* 条件变量ID */

    /* NPC基本信息 */
    int npc_id;
    int running;            /* 运行标志 */

    /* 感知缓冲区 */
    char inbox[256];        /* 收到的消息内容 */
    int inbox_len;
    int inbox_from;         /* 发送者ID, -1表示无 */

    /* 发送缓冲区 */
    char outbox[256];       /* 待发消息内容 */
    int outbox_len;
    int outbox_to;          /* 目标ID, -1表示[to none] */

    /* 状态标志 */
    int has_new_msg;        /* 有新消息 */
    int has_pending_send;   /* 有待发消息 */

    /* 统计 */
    int msg_sent;
    int msg_recv;
    int think_count;
};
```

#### 2. 四线程职责实现

**perception_thread (感知线程)**
- 周期性检查是否有新消息
- 阶段一模拟收到消息（每10轮模拟一次）
- 收到消息后通过 `condvar_signal()` 通知 thinking 线程

**thinking_thread (思考线程)**
- 两种触发条件：
  1. 收到新消息 (`has_new_msg == 1`)
  2. 随机主动发起 (`rand() % 100 < 20`)
- 决策结果：
  - 如果是回复消息：`outbox_to = inbox_from`
  - 如果是主动发起：随机选择目标或 `[to none]`
- 阶段一使用固定消息模板，阶段三改为 AI 调用

**communication_thread (沟通线程)**
- 检查 `has_pending_send` 标志
- 如果 `outbox_to > 0`：发送消息（阶段一模拟输出）
- 如果 `outbox_to < 0`：`[to none]` 不发送
- 发送后触发 IPC 奖励

**memory_thread (记忆线程)**
- 阶段一只是占位
- 阶段三实现三级记忆管理

#### 3. 同步机制

使用现有的线程同步原语：
```c
/* 创建 */
mutex_id = mutex_blocking_create();  /* 阻塞式互斥锁 */
cond_id = condvar_create();          /* 条件变量 */

/* 使用 */
mutex_lock(mutex_id);
/* 临界区操作 */
condvar_signal(cond_id);  /* 通知等待线程 */
mutex_unlock(mutex_id);
```

#### 4. 主线程循环

```c
while (1) {
    npc_get_status(&st);          /* 获取进化调度状态 */

    if (st.my_prio <= 0) {        /* 检查死亡 */
        shared.running = 0;
        break;
    }

    npc_yield();                   /* 触发进化调度 */
}

/* 等待工作线程结束 */
waittid(t_percept);
waittid(t_think);
waittid(t_comm);
waittid(t_memory);
```

#### 5. 随机数初始化

```c
srand(npc_id * 12345 + get_mtime());  /* 每个NPC不同种子 */
```

使用 NPC ID 和系统时间组合，确保不同 NPC 有不同的随机序列。

### Makefile 变更

```makefile
# ch11: NPC社交系统
CH11_BASE_TESTS := $(CH10_BASE_TESTS) ch11_
CH11_TESTS := $(CH11_BASE_TESTS)

# 章节选择
else ifeq ($(CHAPTER), 11)
    CH_TESTS := $(CH11_TESTS)
```

### 预期运行输出

```
========================================
ch11: NPC Social World Started
ch11: Four-thread architecture enabled
ch11: Spawning 3 NPCs...
========================================

ch11: NPC 1 spawned (pid=3)
ch11: NPC 2 spawned (pid=4)
ch11: NPC 3 spawned (pid=5)

ch11: All NPCs spawned, entering world loop...

[NPC 1] Born! Initializing 4-thread architecture...
[NPC 1] Sync primitives created: mutex=0, cond=0
[NPC 1] 4 threads created: percept=1, think=2, comm=3, memory=4
[NPC 1][perception] Thread started
[NPC 1][thinking] Thread started
[NPC 1][comm] Thread started
[NPC 1][memory] Thread started

[NPC 1] tick=0, prio=50, alive=3, sent=0, recv=0
[NPC 1][thinking] Triggered randomly (proactive)
[NPC 1][thinking] Decision: [to npc2]: Hello NPC 2! How are you?
[NPC 1][comm] Sending to NPC 2: "Hello NPC 2! How are you?"
[NPC 1][comm] IPC reward triggered

... (类似输出) ...

[NPC 3] I'm dying... (prio=0)
[NPC 3] Waiting for threads to exit...
ch11: NPC 3 exited (pid=5, code=0)

========================================
ch11: All NPCs dead, world ends
========================================
```

### 待阶段二/三实现的占位

1. **perception_thread**: 真正从管道读取消息
2. **communication_thread**: 真正写入管道
3. **thinking_thread**: 调用 `ai_chat()` 获取决策
4. **memory_thread**: 实现三级记忆存储

### 遇到的技术问题

#### 问题1: 线程退出方式
**现象**: 工作线程需要在 NPC 死亡时优雅退出

**解决**: 使用 `shared.running` 标志，主线程设置为0，工作线程检查后 `exit(0)`

#### 问题2: 条件变量等待
**现象**: `condvar_wait()` 可能无限阻塞

**解决**: 阶段一暂不使用 `condvar_wait()`，改用轮询 + `sched_yield()`

---

## 阶段二：NPC间管道通信

### 完成时间
2025-12-25

### 文件变更

| 文件 | 操作 | 说明 |
|------|------|------|
| `user/src/ch11_world.c` | 修改 | 创建管道矩阵，传递fd给NPC |
| `user/src/ch11_npc.c` | 修改 | 解析管道fd，真正读写管道 |
| `user/lib/syscall_ids.h` | 修改 | 添加SYS_npc_ipc_notify |
| `user/include/ai_sched.h` | 修改 | 添加npc_ipc_notify声明 |
| `user/lib/ai_sched.c` | 修改 | 添加npc_ipc_notify实现 |
| `os/syscall_ids.h` | 修改 | 添加SYS_npc_ipc_notify |
| `os/syscall.c` | 修改 | 添加sys_npc_ipc_notify处理 |

### 实现细节

#### 1. 新增系统调用 `npc_ipc_notify`

```c
/* 系统调用号 */
#define SYS_npc_ipc_notify 486

/* 用户态接口 */
int npc_ipc_notify(int target_pid, int bytes);

/* 内核实现 */
int sys_npc_ipc_notify(int target_pid, int bytes)
{
    ai_sched_add_ipc(target_pid, bytes);
    return 0;
}
```

**功能**: 通知内核发生了NPC间通信，用于累计IPC流量并触发奖励。

#### 2. 管道矩阵创建 (ch11_world.c)

```c
/* pipes[from][to][0/1] */
/* pipes[i][j][0] = 读端, pipes[i][j][1] = 写端 */
static int pipes[NPC_COUNT][NPC_COUNT][2];

/* 创建管道 */
for (i = 0; i < NPC_COUNT; i++) {
    for (j = 0; j < NPC_COUNT; j++) {
        if (i != j) {
            int pfd[2];
            pipe(pfd);
            pipes[i][j][0] = pfd[0];  /* 读端 */
            pipes[i][j][1] = pfd[1];  /* 写端 */
        }
    }
}
```

#### 3. 管道fd传递

通过命令行参数传递fd给NPC进程：
```
ch11_npc <id> <r0> <r1> <w0> <w1>
```

对于3个NPC (NPC_COUNT=3):
- NPC 1: `ch11_npc 1 <从NPC2读> <从NPC3读> <写到NPC2> <写到NPC3>`
- NPC 2: `ch11_npc 2 <从NPC1读> <从NPC3读> <写到NPC1> <写到NPC3>`
- NPC 3: `ch11_npc 3 <从NPC1读> <从NPC2读> <写到NPC1> <写到NPC2>`

#### 4. npc_shared 结构扩展

```c
struct npc_shared {
    /* ... 原有字段 ... */

    /* ch11阶段二: 管道fd数组 */
    int read_fds[NPC_COUNT - 1];
    int write_fds[NPC_COUNT - 1];
    int read_from_npc[NPC_COUNT - 1];  /* read_fds[i]对应的NPC ID */
    int write_to_npc[NPC_COUNT - 1];   /* write_fds[i]对应的NPC ID */

    /* ch11阶段二: 目标进程PID (用于IPC奖励) */
    int target_pids[NPC_COUNT];
};
```

#### 5. perception_thread 真正读管道

```c
/* 轮询所有读管道 */
for (i = 0; i < NPC_COUNT - 1; i++) {
    int fd = shared->read_fds[i];
    if (fd < 0) continue;

    int n = read(fd, buf, sizeof(buf) - 1);
    if (n > 0) {
        buf[n] = '\0';
        mutex_lock(shared->mutex_id);
        /* 存入inbox */
        str_copy(shared->inbox, buf, sizeof(shared->inbox));
        shared->inbox_from = shared->read_from_npc[i];
        shared->has_new_msg = 1;
        shared->msg_recv++;
        condvar_signal(shared->cond_id);
        mutex_unlock(shared->mutex_id);
    }
}
```

#### 6. communication_thread 真正写管道

```c
/* 找到目标NPC的写fd */
int idx = find_write_fd_idx(shared, shared->outbox_to);
if (idx >= 0 && shared->write_fds[idx] >= 0) {
    int n = write(shared->write_fds[idx], shared->outbox, shared->outbox_len);
    if (n > 0) {
        /* 触发IPC奖励 */
        int target_pid = shared->target_pids[shared->outbox_to - 1];
        npc_ipc_notify(target_pid, n);
        shared->msg_sent++;
    }
}
```

### 预期运行输出

```
========================================
ch11: NPC Social World Started
ch11: Four-thread architecture + Pipe IPC
ch11: Spawning 3 NPCs...
========================================

ch11: Creating pipe matrix...
ch11: pipe(1->2) created: r=3, w=4
ch11: pipe(1->3) created: r=5, w=6
ch11: pipe(2->1) created: r=7, w=8
ch11: pipe(2->3) created: r=9, w=10
ch11: pipe(3->1) created: r=11, w=12
ch11: pipe(3->2) created: r=13, w=14

ch11: NPC 1 spawned (pid=3)
ch11: NPC 2 spawned (pid=4)
ch11: NPC 3 spawned (pid=5)

[NPC 1] Born! argc=6
[NPC 1] read_fd[0]=7 (from NPC 2)
[NPC 1] read_fd[1]=11 (from NPC 3)
[NPC 1] write_fd[0]=4 (to NPC 2)
[NPC 1] write_fd[1]=6 (to NPC 3)

[NPC 1][thinking] Triggered randomly (proactive)
[NPC 1][thinking] Decision: [to npc2]: Hello NPC 2! How are you?
[NPC 1][comm] Sent to NPC 2 via pipe: "Hello NPC 2! How are you?" (25 bytes)
[NPC 1][comm] IPC reward notified (target_pid=4)

[NPC 2][perception] Received from NPC 1: "Hello NPC 2! How are you?"
[NPC 2][thinking] Triggered by new message
[NPC 2][thinking] Decision: [to npc1]: Reply to NPC 1: Got your message!
[NPC 2][comm] Sent to NPC 1 via pipe: "Reply to NPC 1: Got your message!" (34 bytes)
[NPC 2][comm] IPC reward notified (target_pid=3)

... (NPC们通过管道互相发消息) ...

ch10: [OOM Killer] NPC 3 died (prio=0)
[NPC 3] I'm dying... (prio=0)

========================================
ch11: All NPCs dead, world ends
========================================
```

### 技术要点

1. **管道方向**: `pipes[from][to]` 表示从NPC from发送到NPC to的管道
2. **fd继承**: fork后子进程继承父进程的fd表，所以管道fd可以跨进程使用
3. **非阻塞读**: 当前实现使用轮询+sched_yield()，简化处理
4. **IPC奖励**: 发送消息后调用`npc_ipc_notify(target_pid, bytes)`通知内核

### 遇到的技术问题

#### 问题1: 管道fd顺序
**现象**: NPC需要知道哪个fd对应哪个NPC

**解决**: 使用`read_from_npc[]`和`write_to_npc[]`数组记录映射关系

#### 问题2: 目标PID获取
**现象**: IPC奖励需要目标进程的PID

**解决**: 简化处理，假设NPC按顺序fork，PID从3开始递增

#### 问题3: sys_pipe() 数据类型不匹配 (内核bug)
**现象**: 管道创建后，写端fd总是0，读端fd正确。NPC写入fd=0实际写到了stdout而不是管道。

**原因**: 内核`sys_pipe()`使用`sizeof(uint64)`(8字节)写fd到用户空间，但用户态`int pfd[2]`期望`sizeof(int)`(4字节)。
```c
/* 错误的写法 */
copyout(p->pagetable, fdarray, (char *)&fd0, sizeof(fd0));  /* fd0是uint64 */
copyout(p->pagetable, fdarray + sizeof(uint64), (char *)&fd1, sizeof(fd1));
```
内存布局问题：
- 内核写入: `[fd0的8字节][fd1的8字节]`
- 用户读取: `pfd[0]`读前4字节=正确, `pfd[1]`读第5-8字节=0

**解决**: 修改`os/syscall.c`中的`sys_pipe()`，使用`sizeof(int)`:
```c
int fd0_int = (int)fd0;
int fd1_int = (int)fd1;
copyout(p->pagetable, fdarray, (char *)&fd0_int, sizeof(int));
copyout(p->pagetable, fdarray + sizeof(int), (char *)&fd1_int, sizeof(int));
```

#### 问题4: sys_pipe() 未初始化变量检查
**现象**: 原代码有无意义的检查
```c
struct file *f0, *f1;
if (f0 < 0 || f1 < 0) {  /* f0/f1未初始化，比较无意义 */
    return -1;
}
```

**解决**: 改为先分配再检查NULL:
```c
f0 = filealloc();
f1 = filealloc();
if (f0 == NULL || f1 == NULL) {
    if (f0) fileclose(f0);
    if (f1) fileclose(f1);
    return -1;
}
```

#### 问题5: perception线程阻塞导致进程无法退出
**现象**: NPC死亡后，主线程在`waittid(t_percept)`处卡住，因为perception线程阻塞在`read()`上。

**原因**:
1. 内核的`piperead()`实现：当管道为空且写端开着时，会调用`yield()`循环等待
2. 关闭自己进程的管道fd不会影响其他进程持有的同一管道的写端引用
3. fork后每个进程都继承了所有管道fd，所以写端永远不会真正关闭

**解决**: 不等待perception线程，直接退出。进程退出时内核会清理所有线程资源。
```c
/* 先等待不会阻塞的线程 */
waittid(t_think);
waittid(t_comm);
waittid(t_memory);

/* perception线程可能阻塞，给它几轮调度机会 */
for (i = 0; i < 5; i++) {
    sched_yield();
}

/* 不再等待perception线程，直接退出 */
```

#### 问题6: 用户态syscall_ids.h被构建系统覆盖
**现象**: 在`user/lib/syscall_ids.h`中添加`SYS_npc_ipc_notify`后，编译报错"undeclared"

**原因**: CMake构建系统会从`user/lib/arch/riscv/syscall_ids.h.in`自动生成`syscall_ids.h`:
```cmake
sed -n -e s/__NR_/SYS_/p < syscall_ids.h.in > lib/syscall_ids.h
```

**解决**: 在`.in`源文件中添加(使用`__NR_`前缀):
```c
/* ch11: IPC通知系统调用 */
#define __NR_npc_ipc_notify 486
```

---

## 阶段三：AI驱动决策+三级记忆（待实现）

### 计划实现

1. **thinking线程调用AI**
   ```c
   ai_chat(prompt, response, sizeof(response));
   ```

2. **解析AI返回**
   ```c
   parse_ai_response(response, &to_npc, &message, &memory);
   ```

3. **三级记忆**
   - L1: 硬编码人设
   - L2: `npc_memory_save()` / `npc_memory_load()`
   - L3: 进程内 `session_history[]` 数组

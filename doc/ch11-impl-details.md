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

## 阶段二：NPC间管道通信（待实现）

### 计划实现

1. **world 创建管道矩阵**
   ```c
   int pipes[NPC_COUNT][NPC_COUNT][2];
   ```

2. **传递管道fd给NPC进程**
   - 通过命令行参数或环境变量

3. **perception线程读管道**
   ```c
   read(pipe_fd, inbox, sizeof(inbox));
   ```

4. **communication线程写管道**
   ```c
   write(pipe_fd, outbox, outbox_len);
   ```

5. **IPC奖励触发**
   - 调用内核 `npc_ipc_notify()` 系统调用

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

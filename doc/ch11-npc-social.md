# Ch11: NPC 社交系统（四线程架构）

## 概述

本章在 ch10 进化调度的基础上，为每个 NPC 实现四线程架构，支持 NPC 间的异步消息传递和 AI 驱动的决策系统。通过社交互动获得 IPC 奖励，NPC 可以延长生存时间。

## 设计目标

1. **四线程架构**: 每个 NPC 运行感知、思考、沟通、记忆四个并发线程
2. **异步消息传递**: NPC 间通过管道通信，类似消息队列（发送后不等回复）
3. **AI 驱动决策**: thinking 线程调用外部 AI API 生成结构化响应
4. **三级记忆系统**: 人设（永久）→ AI记忆（持久）→ 会话历史（临时）
5. **主动发起机制**: 随机数驱动，NPC 有概率主动发起社交，不依赖被动接收消息

## 系统架构

```
┌─────────────────────────────────────────────────────────────────┐
│                        ch11_world                                │
│                      (世界管理器)                                │
└───────────────────────────┬─────────────────────────────────────┘
                            │ fork + exec
        ┌───────────────────┼───────────────────┐
        ▼                   ▼                   ▼
┌───────────────┐   ┌───────────────┐   ┌───────────────┐
│    NPC 1      │   │    NPC 2      │   │    NPC 3      │
│ ┌───────────┐ │   │ ┌───────────┐ │   │ ┌───────────┐ │
│ │perception │ │   │ │perception │ │   │ │perception │ │
│ │ 感知线程   │ │   │ │ 感知线程   │ │   │ │ 感知线程   │ │
│ └───────────┘ │   │ └───────────┘ │   │ └───────────┘ │
│ ┌───────────┐ │   │ ┌───────────┐ │   │ ┌───────────┐ │
│ │ thinking  │ │   │ │ thinking  │ │   │ │ thinking  │ │
│ │ 思考线程   │ │   │ │ 思考线程   │ │   │ │ 思考线程   │ │
│ └───────────┘ │   │ └───────────┘ │   │ └───────────┘ │
│ ┌───────────┐ │   │ ┌───────────┐ │   │ ┌───────────┐ │
│ │   comm    │ │   │ │   comm    │ │   │ │   comm    │ │
│ │ 沟通线程   │ │   │ │ 沟通线程   │ │   │ │ 沟通线程   │ │
│ └───────────┘ │   │ └───────────┘ │   │ └───────────┘ │
│ ┌───────────┐ │   │ ┌───────────┐ │   │ ┌───────────┐ │
│ │  memory   │ │   │ │  memory   │ │   │ │  memory   │ │
│ │ 记忆线程   │ │   │ │ 记忆线程   │ │   │ │ 记忆线程   │ │
│ └───────────┘ │   │ └───────────┘ │   │ └───────────┘ │
└───────┬───────┘   └───────┬───────┘   └───────┬───────┘
        │                   │                   │
        └─────────── 管道通信 ──────────────────┘
```

## 四线程职责

| 线程 | 职责 | 输入 | 输出 |
|------|------|------|------|
| **perception** | 检查管道，接收其他NPC消息 | 管道数据 | 更新感知缓冲区 |
| **thinking** | 调用AI API，产生结构化决策 | 感知+记忆 | 待发消息、待存记忆 |
| **communication** | 把待发消息写入目标NPC管道 | 待发队列 | 触发IPC奖励 |
| **memory** | 管理三级记忆 | AI返回的[memory] | 更新记忆存储 |

## 实现分三阶段

### 阶段一：四线程框架

使用现有 `thread_create()` 创建四个线程，用互斥锁和条件变量协调。

**共享数据结构**:
```c
struct npc_shared {
    /* 互斥锁 */
    int mutex_id;
    int cond_id;

    /* 感知缓冲区 */
    char inbox[512];        /* 收到的消息 */
    int inbox_len;
    int inbox_from;         /* 发送者NPC ID */

    /* 待发消息队列 */
    char outbox[512];       /* 待发送的消息 */
    int outbox_len;
    int outbox_to;          /* 目标NPC ID */

    /* 状态标志 */
    int has_new_msg;        /* 有新消息待处理 */
    int has_pending_send;   /* 有消息待发送 */
    int running;            /* NPC是否运行中 */
};
```

**线程入口**:
```c
void perception_thread(void *arg);
void thinking_thread(void *arg);
void communication_thread(void *arg);
void memory_thread(void *arg);
```

### 阶段二：NPC 间管道通信

NPC 间通过管道传递消息，类似消息队列机制。

**通信模型**:
```
NPC 1                                    NPC 2
┌──────────┐                            ┌──────────┐
│ thinking │ 产生消息                    │perception│ 轮询检查
│  "你好"   │                            │          │
└────┬─────┘                            └────▲─────┘
     │                                       │
     ▼                                       │
┌──────────┐      pipe[1->2]           ┌─────┴────┐
│   comm   │ ────────────────────────► │ 收到消息  │
│  发送端   │  write("你好")            │  read()  │
└──────────┘                           └──────────┘
     │
     ▼
[IPC Reward] NPC 1 优先级 +1
```

**特点**:
- 异步通信：发送后不等待回复（像微信）
- perception 线程非阻塞轮询管道
- 成功发送/接收触发 IPC 奖励

**管道管理**:
```c
/* 世界管理器创建 NPC 间管道 */
int pipes[NPC_COUNT][NPC_COUNT][2];  /* pipes[from][to][read/write] */

/* NPC 1 发消息给 NPC 2 */
write(pipes[1][2][1], msg, len);     /* 写入 pipe[1->2] 的写端 */

/* NPC 2 接收来自 NPC 1 的消息 */
read(pipes[1][2][0], buf, size);     /* 读取 pipe[1->2] 的读端 */
```

### 阶段三：AI 驱动决策 + 三级记忆

**AI 返回格式**:
```
[to npc2]: 你好，今天天气不错！
[memory]: npc2主动跟我打招呼，她好像挺友好的
```

**解析规则**:
- `[to npcX]:` 后面是发给 npcX 的消息
- `[to none]:` 表示这轮不发消息给任何人（沉默/思考中）
- `[memory]:` 后面是需要存入二级记忆的内容
- 可以有多个 `[to]` 和 `[memory]` 标签

**主动发起机制**（随机数驱动）:
```c
/* ch11: thinking 线程的触发条件 */
int should_think = 0;

/* 条件1: 收到新消息 */
if (shared->has_new_msg)
    should_think = 1;

/* 条件2: 随机主动发起 (概率约20%) */
if (rand() % 100 < 20)
    should_think = 1;

if (should_think) {
    /* 调用 AI API */
    /* AI 可能返回 [to none] 表示不说话 */
}
```

这样 NPC 不只是被动响应，也会主动找人聊天，更像真实社交行为。

**三级记忆系统**:

| 级别 | 名称 | 内容示例 | 存储位置 | 生命周期 |
|------|------|---------|---------|---------|
| L1 | 人设 | "我是小明，性格开朗，喜欢交朋友" | 代码硬编码 | 永久 |
| L2 | AI记忆 | "npc2很友好，npc3有点冷淡" | 内核专用内存 | 持久 |
| L3 | 会话历史 | 本轮所有对话记录 | 进程内存 | 临时 |

**内核记忆区**:
```c
/* os/npc_memory.c */
#define NPC_MAX 16
#define MEMORY_SIZE 4096

struct npc_memory {
    int npc_id;
    char l2_memory[MEMORY_SIZE];  /* 二级记忆 */
    int l2_len;
};

static struct npc_memory memories[NPC_MAX];
```

**新增系统调用**:
```c
/* 保存二级记忆 */
int npc_memory_save(int npc_id, char *content, int len);

/* 读取二级记忆 */
int npc_memory_load(int npc_id, char *buf, int maxlen);
```

## 完整流程

```
┌─────────────────────────────────────────────────────────────────┐
│                         一轮循环                                 │
└─────────────────────────────────────────────────────────────────┘

1. perception线程
   ├── 非阻塞读取管道
   ├── 如果有消息: 存入 inbox, 设置 has_new_msg = 1
   └── 通知 thinking 线程 (condvar_signal)

2. thinking线程
   ├── 等待条件变量 (有新消息或定时触发)
   ├── 构建 prompt:
   │   ├── L1: 人设 (硬编码)
   │   ├── L2: AI记忆 (npc_memory_load)
   │   └── L3: 会话历史 (进程内存)
   ├── 调用 ai_chat(prompt)
   ├── 解析 AI 返回:
   │   ├── [to npcX]: 存入 outbox, 设置 outbox_to = X
   │   └── [memory]: 存入待保存队列
   └── 通知 comm 和 memory 线程

3. communication线程
   ├── 等待 has_pending_send
   ├── 写入目标 NPC 管道: write(pipe[me][to], outbox, len)
   ├── 触发 IPC 奖励: npc_yield() (内核计算奖励)
   └── 清空 outbox

4. memory线程
   ├── 等待有新记忆需要保存
   ├── 调用 npc_memory_save() 存入 L2
   └── 更新 L3 会话历史

5. 主线程
   ├── npc_yield() 触发进化调度
   ├── 检查优先级，决定是否继续
   └── 循环
```

## 预期运行输出

```
========================================
ch11: NPC World Started
ch11: Creating pipes between NPCs...
ch11: Spawning 3 NPCs...
========================================

[NPC 1] Born! Starting 4 threads...
[NPC 1][perception] Thread started
[NPC 1][thinking] Thread started
[NPC 1][comm] Thread started
[NPC 1][memory] Thread started

[NPC 2] Born! Starting 4 threads...
[NPC 3] Born! Starting 4 threads...

[NPC 1][thinking] Building prompt with L1+L2+L3...
[NPC 1][thinking] Calling AI API...
[NPC 1][thinking] AI response: "[to npc2]: 你好！[memory]: 第一次主动打招呼"
[NPC 1][comm] Sending "你好！" to NPC 2
[IPC Reward] NPC 1 +1 (prio: 50->51)
[NPC 1][memory] Saved to L2: "第一次主动打招呼"

[NPC 2][perception] Received "你好！" from NPC 1
[IPC Reward] NPC 2 +1 (prio: 50->51)
[NPC 2][thinking] Building prompt: "NPC 1 said: 你好！"
[NPC 2][thinking] Calling AI API...
[NPC 2][thinking] AI response: "[to npc1]: 你也好！[memory]: npc1很友好"
[NPC 2][comm] Sending "你也好！" to NPC 1
[IPC Reward] NPC 2 +1 (prio: 51->52)

... (NPC们互相聊天，通过IPC奖励维持优先级) ...

[NPC 3] tick=30, prio=20, alive=3
[NPC 3][thinking] AI API timeout...
[NPC 3] No social activity, priority decaying...

... (不社交的NPC优先级下降) ...

[OOM Killer] NPC 3 killed (prio=0)
[NPC 3] I'm dying...

... (剩余NPC继续社交) ...

========================================
ch11: All NPCs dead, world ends
========================================
```

## 文件结构

```
os/
├── npc_memory.c        # 新增：NPC记忆管理（L2存储）
├── npc_memory.h        # 新增：记忆系统头文件
├── ai_sched.c          # 修改：添加IPC奖励触发
└── syscall.c           # 修改：新增 npc_memory_save/load

user/
├── src/
│   ├── ch11_world.c    # 新增：世界管理器（创建管道）
│   └── ch11_npc.c      # 新增：NPC进程（四线程）
├── lib/
│   └── npc.c           # 新增：NPC用户态库
└── include/
    └── npc.h           # 新增：NPC头文件
```

## 新增系统调用

| 系统调用 | ID | 参数 | 功能 |
|---------|----|----|------|
| npc_memory_save | 486 | (npc_id, content, len) | 保存L2记忆 |
| npc_memory_load | 487 | (npc_id, buf, maxlen) | 读取L2记忆 |
| npc_ipc_notify | 488 | (from_id, to_id, bytes) | 通知内核IPC发生 |

## IPC 奖励机制

```c
/* os/ai_sched.c */
void ai_sched_ipc_reward(int from_npc, int to_npc, int bytes) {
    if (bytes >= NPC_IPC_REWARD_BYTES) {
        int reward = bytes / NPC_IPC_REWARD_BYTES;

        /* 发送方奖励 */
        struct proc *sender = find_npc_proc(from_npc);
        if (sender) {
            sender->dynamic_prio += reward;
            if (sender->dynamic_prio > NPC_PRIO_MAX)
                sender->dynamic_prio = NPC_PRIO_MAX;
        }

        /* 接收方奖励 */
        struct proc *receiver = find_npc_proc(to_npc);
        if (receiver) {
            receiver->dynamic_prio += reward;
            if (receiver->dynamic_prio > NPC_PRIO_MAX)
                receiver->dynamic_prio = NPC_PRIO_MAX;
        }

        printf("[IPC Reward] NPC %d +%d, NPC %d +%d\n",
               from_npc, reward, to_npc, reward);
    }
}
```

## 与 ch10 的关系

| ch10 | ch11 |
|------|------|
| 优先级衰减机制 | 继续使用 |
| OOM Killer | 继续使用 |
| npc_register/get_status/yield | 继续使用 |
| IPC奖励接口（预留） | **正式启用** |
| 单线程NPC | **升级为四线程** |

## 注意事项

1. **管道缓冲区**: 512字节，消息过长需截断
2. **AI API 超时**: thinking 线程需设置超时，避免阻塞
3. **线程同步**: 共享数据必须用互斥锁保护
4. **内存限制**: L2记忆区每个NPC限制4KB
5. **L3会话清理**: 当进程内存紧张时，L3可能被清理

## 下一步：ch12

ch12 可以考虑：
- NPC 情感系统（好感度、信任度）
- 更复杂的社交网络
- NPC 组队/对抗机制

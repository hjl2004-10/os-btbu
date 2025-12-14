# Ch10: 进化调度（NPC沙盒）

## 概述

本章实现了一个"进化调度"机制，为后续AI小镇项目奠定基础。系统将每个NPC作为独立进程运行，通过优先级衰减和IPC奖励机制模拟自然选择压力。

## 设计目标

1. **优先级衰减**: 每个时钟tick，所有NPC的优先级自动减1
2. **IPC奖励**: NPC之间有效通信可获得优先级奖励（ch11实现）
3. **OOM Killer**: 优先级归零的NPC被系统淘汰
4. **可观测性**: 提供状态查询接口，便于监控NPC生存状况

## 系统架构

```
+------------------+
|   ch10_world     |  <- 世界管理器（用户态）
|  (fork + exec)   |
+------------------+
        |
        v
+-------+--------+--------+
|       |        |        |
v       v        v        v
ch10_npc ch10_npc ch10_npc ...  <- NPC进程（用户态）
        |        |        |
        v        v        v
+----------------------------------+
|        内核调度器                 |
|   ai_sched_tick() - 优先级衰减   |
|   OOM Killer - 淘汰低优先级NPC   |
+----------------------------------+
```

## 系统调用

### npc_register (syscall 483)

注册当前进程为NPC。

```c
int npc_register(int npc_id);
```

**参数**:
- `npc_id`: NPC的唯一标识符

**返回值**:
- 0: 成功
- -1: 失败（如已注册过）

**内核实现**:
```c
int sys_npc_register(void) {
    int npc_id;
    argint(0, &npc_id);

    struct proc *p = myproc();
    if (p->is_npc)
        return -1;  /* 已注册 */

    p->is_npc = 1;
    p->npc_id = npc_id;
    p->dynamic_prio = NPC_INIT_PRIO;  /* 初始优先级50 */

    return 0;
}
```

### npc_get_status (syscall 484)

获取当前NPC的状态信息。

```c
int npc_get_status(struct npc_status *st);
```

**参数**:
- `st`: 输出参数，存放状态信息

**npc_status 结构体**:
```c
struct npc_status {
    int tick;       /* 当前系统时钟 */
    int alive_npcs; /* 存活NPC数量 */
    int my_prio;    /* 本NPC当前优先级 */
};
```

**返回值**:
- 0: 成功
- -1: 失败（非NPC进程调用）

### npc_yield (syscall 485)

NPC主动让出CPU，触发进化调度。

```c
void npc_yield(void);
```

该调用会触发 `ai_sched_tick()`，执行优先级衰减和OOM检查。

## 调度参数

```c
/* os/ai_sched.h */
#define NPC_INIT_PRIO       50   /* 初始优先级 */
#define NPC_DECAY_RATE      1    /* 每tick衰减量 */
#define NPC_IPC_REWARD_BYTES 64  /* IPC奖励阈值（字节） */
#define NPC_DEATH_THRESHOLD 0    /* 死亡阈值 */
#define NPC_PRIO_MAX        100  /* 最大优先级 */
```

## 核心算法

### 优先级衰减 (ai_sched_tick)

```c
void ai_sched_tick(void) {
    struct proc *p;

    for (p = proc; p < &proc[NPROC]; p++) {
        acquire(&p->lock);

        /* ch10: 跳过非NPC进程 */
        if (!p->is_npc || p->state == UNUSED) {
            release(&p->lock);
            continue;
        }

        /* ch10: 跳过已死亡的NPC */
        if (p->dynamic_prio < 0) {
            release(&p->lock);
            continue;
        }

        /* ch10: 优先级衰减 */
        p->dynamic_prio -= NPC_DECAY_RATE;

        /* ch10: OOM Killer */
        if (p->dynamic_prio <= NPC_DEATH_THRESHOLD) {
            printf("[OOM Killer] NPC %d killed (prio=%d)\n",
                   p->npc_id, p->dynamic_prio);
            p->dynamic_prio = -1;  /* 标记为已死亡 */
            p->killed = 1;
        }

        release(&p->lock);
    }

    global_tick++;
}
```

### IPC奖励（ch11预留）

```c
void ai_sched_ipc_reward(struct proc *sender, struct proc *receiver, int bytes) {
    if (bytes >= NPC_IPC_REWARD_BYTES) {
        int reward = bytes / NPC_IPC_REWARD_BYTES;

        sender->dynamic_prio += reward;
        if (sender->dynamic_prio > NPC_PRIO_MAX)
            sender->dynamic_prio = NPC_PRIO_MAX;

        receiver->dynamic_prio += reward;
        if (receiver->dynamic_prio > NPC_PRIO_MAX)
            receiver->dynamic_prio = NPC_PRIO_MAX;
    }
}
```

## 用户态程序

### ch10_world.c - 世界管理器

```c
int main() {
    printf("[World] Initializing NPC world...\n");

    /* 创建3个NPC进程 */
    for (int i = 0; i < 3; i++) {
        int pid = fork();
        if (pid == 0) {
            char id_str[8];
            int_to_str(i, id_str);
            char *argv[] = {"ch10_npc", id_str, 0};
            exec("ch10_npc", argv);
            exit(-1);
        }
    }

    /* 等待所有NPC死亡 */
    while (wait(0) > 0);

    printf("[World] All NPCs died, world ended\n");
    return 0;
}
```

### ch10_npc.c - NPC进程

```c
int main(int argc, char *argv[]) {
    int npc_id = str_to_int(argv[1]);
    struct npc_status st;

    printf("[NPC %d] Born!\n", npc_id);

    /* 向内核注册 */
    npc_register(npc_id);

    /* 主循环 */
    while (1) {
        npc_get_status(&st);

        /* 检查是否死亡 */
        if (st.my_prio <= 0) {
            printf("[NPC %d] I'm dying...\n", npc_id);
            break;
        }

        /* ch11: 这里将添加社交行为 */

        npc_yield();  /* 触发进化调度 */
    }

    return 0;
}
```

## 测试结果

运行 `ch10_world` 的典型输出：

```
[World] Initializing NPC world...
[NPC 0] Born!
[NPC 1] Born!
[NPC 2] Born!
[NPC 0] tick=5, prio=45, alive=3
[NPC 1] tick=5, prio=45, alive=3
[NPC 2] tick=5, prio=45, alive=3
...
[NPC 0] tick=45, prio=5, alive=3
[NPC 1] tick=45, prio=5, alive=3
[OOM Killer] NPC 0 killed (prio=0)
[NPC 0] I'm dying... (prio=0)
[OOM Killer] NPC 1 killed (prio=0)
[NPC 1] I'm dying... (prio=0)
[OOM Killer] NPC 2 killed (prio=0)
[NPC 2] I'm dying... (prio=0)
[World] All NPCs died, world ended
```

所有NPC经过约50轮后因优先级耗尽而死亡，符合预期。

## 文件结构

```
os/
├── ai_sched.c      # 进化调度核心实现
├── ai_sched.h      # 调度参数和结构体定义
└── syscall.c       # 系统调用处理（新增3个syscall）

user/
├── src/
│   ├── ch10_world.c    # 世界管理器
│   └── ch10_npc.c      # NPC进程
├── lib/
│   └── ai_sched.c      # 用户态syscall封装
└── include/
    └── ai_sched.h      # 用户态头文件
```

## 与AI小镇的关系

本章是AI小镇项目的基础设施层，实现了：

1. **生存压力模拟**: 优先级衰减模拟资源消耗
2. **自然选择机制**: OOM Killer淘汰适应力差的个体
3. **协作激励**: IPC奖励鼓励NPC之间的社交互动

后续章节将在此基础上实现：
- **ch11**: NPC社交通信（消息传递、IPC奖励生效）
- **ch12+**: AI驱动的NPC行为决策

## 下一步：ch11 NPC社交系统

ch11将实现NPC之间的消息传递机制：

```c
/* 新增系统调用 */
int npc_send(int to_npc_id, char *msg, int len);
int npc_recv(char *buf, int maxlen, int *from_npc_id);
```

NPC通过有效社交可获得优先级奖励，延长生存时间。

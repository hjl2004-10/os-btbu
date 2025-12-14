/* ch10: 进化调度器头文件 */
#ifndef AI_SCHED_H
#define AI_SCHED_H

#include "types.h"

/* ch10: 进化调度参数 */
#define NPC_INIT_PRIO       50      /* NPC初始优先级 */
#define NPC_DECAY_RATE      1       /* 每tick优先级衰减量 */
#define NPC_IPC_REWARD_BYTES 64     /* 每N字节IPC奖励1点优先级 */
#define NPC_DEATH_THRESHOLD 0       /* 低于此值死亡 */
#define NPC_MAX_COUNT       8       /* 最大NPC数量 */
#define NPC_PRIO_MAX        100     /* 优先级上限 */

/* ch10: NPC状态结构 - 用户态可查询 */
struct npc_status {
    int npc_id;         /* NPC编号 */
    int my_prio;        /* 当前优先级 (生命值) */
    int my_memory;      /* 内存占用 (页数) */
    int world_load;     /* 系统负载 (存活NPC数) */
    int alive_npcs;     /* 存活NPC数量 */
    int tick;           /* 当前世界tick */
};

/* ch10: 内核函数声明 */
void ai_sched_init(void);                           /* 初始化调度器 */
void ai_sched_tick(void);                           /* 调度tick (优先级衰减+淘汰) */
int  ai_sched_register(int npc_id);                 /* 注册NPC */
int  ai_sched_get_status(struct npc_status *st);    /* 获取状态 */
void ai_sched_add_ipc(int pid, uint64 bytes);       /* 增加IPC流量 */
int  ai_sched_get_alive_count(void);                /* 获取存活NPC数 */

#endif /* AI_SCHED_H */

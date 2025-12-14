/* ch10: 进化调度用户态接口 */

#ifndef __AI_SCHED_H__
#define __AI_SCHED_H__

/* ch10: 进化调度 - NPC状态结构 */
struct npc_status {
	int npc_id;         /* NPC编号 */
	int my_prio;        /* 当前优先级 (生命值) */
	int my_memory;      /* 内存占用 (页数) */
	int world_load;     /* 系统负载 */
	int alive_npcs;     /* 存活NPC数量 */
	int tick;           /* 当前世界tick */
};

/* ch10: 进化调度系统调用 */
int npc_register(int npc_id);
int npc_get_status(void *st);
int npc_yield(void);

#endif /* __AI_SCHED_H__ */

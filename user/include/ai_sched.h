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

/* ch11: IPC通知 - 告知内核发生了NPC间通信 */
int npc_ipc_notify(int target_pid, int bytes);

/* ch11: AI聊天 - 用户态调用内核AI API */
int npc_ai_chat(const char *prompt, char *response, int resp_maxlen);

/* ch11: 记忆系统 - L2记忆读写 */
int npc_memory_save(int npc_id, const char *content, int len);
int npc_memory_load(int npc_id, char *buf, int maxlen);

#endif /* __AI_SCHED_H__ */

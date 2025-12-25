/* ch10: 进化调度系统调用包装 */

#include "syscall.h"

/* ch10: 进化调度 - NPC注册 */
int npc_register(int npc_id)
{
	return syscall(SYS_npc_register, npc_id);
}

/* ch10: 进化调度 - 获取NPC状态 */
int npc_get_status(void *st)
{
	return syscall(SYS_npc_get_status, st);
}

/* ch10: 进化调度 - NPC让出CPU */
int npc_yield(void)
{
	return syscall(SYS_npc_yield);
}

/* ch11: IPC通知 - 告知内核发生了NPC间通信 */
int npc_ipc_notify(int target_pid, int bytes)
{
	return syscall(SYS_npc_ipc_notify, target_pid, bytes);
}

/* ch11: AI聊天 - 用户态调用内核AI API */
int npc_ai_chat(const char *prompt, char *response, int resp_maxlen)
{
	return syscall(SYS_npc_ai_chat, prompt, response, resp_maxlen);
}

/* ch11: 记忆系统 - 保存L2记忆 */
int npc_memory_save(int npc_id, const char *content, int len)
{
	return syscall(SYS_npc_memory_save, npc_id, content, len);
}

/* ch11: 记忆系统 - 读取L2记忆 */
int npc_memory_load(int npc_id, char *buf, int maxlen)
{
	return syscall(SYS_npc_memory_load, npc_id, buf, maxlen);
}

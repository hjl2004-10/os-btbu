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

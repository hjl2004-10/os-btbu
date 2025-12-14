/* ch10: NPC主进程 - 单个NPC的生命周期 */
/* 功能: 演示进化调度，为ch11预留4线程接口 */

#include <stdio.h>
#include <unistd.h>
#include <ai_sched.h>

/* ch10: 简单的字符串转整数 */
static int str_to_int(const char *s)
{
	int n = 0;
	while (*s >= '0' && *s <= '9') {
		n = n * 10 + (*s - '0');
		s++;
	}
	return n;
}

/* ch11: 4线程占位函数 - 将在ch11实现 */
/*
static void perception_thread(void *arg) { }
static void thinking_thread(void *arg) { }
static void communication_thread(void *arg) { }
static void memory_thread(void *arg) { }
*/

int main(int argc, char *argv[])
{
	int npc_id;
	struct npc_status st;
	int loop_count = 0;

	/* ch10: 解析NPC ID */
	if (argc < 2) {
		printf("[NPC ?] Error: no NPC ID provided\n");
		return -1;
	}
	npc_id = str_to_int(argv[1]);

	printf("[NPC %d] Born!\n", npc_id);

	/* ch10: 向内核注册 */
	if (npc_register(npc_id) < 0) {
		printf("[NPC %d] Failed to register\n", npc_id);
		return -1;
	}

	/* ch11: 这里将创建4个线程 */
	/*
	thread_create(perception_thread, &npc_id);
	thread_create(thinking_thread, &npc_id);
	thread_create(communication_thread, &npc_id);
	thread_create(memory_thread, &npc_id);
	*/

	/* ch10: 主循环 - 演示进化调度 */
	while (1) {
		/* ch10: 获取当前状态 */
		if (npc_get_status(&st) < 0) {
			printf("[NPC %d] Failed to get status\n", npc_id);
			break;
		}

		/* ch10: 每5轮打印一次状态 */
		if (loop_count % 5 == 0) {
			printf("[NPC %d] tick=%d, prio=%d, alive=%d\n",
				npc_id, st.tick, st.my_prio, st.alive_npcs);
		}

		/* ch10: 检查是否死亡 */
		if (st.my_prio <= 0) {
			printf("[NPC %d] I'm dying... (prio=%d)\n", npc_id, st.my_prio);
			break;
		}

		/* ch10: 模拟简单行为 - 未来ch11会在这里做真实的思考/沟通 */
		/* 当前没有IPC，所以所有NPC都会因为优先级衰减而死亡 */

		/* ch10: 让出CPU，触发进化调度 */
		npc_yield();
		loop_count++;

		/* ch10: 防止无限循环，限制最大轮数 */
		if (loop_count > 100) {
			printf("[NPC %d] Max loops reached, exiting\n", npc_id);
			break;
		}
	}

	printf("[NPC %d] Died after %d loops\n", npc_id, loop_count);
	return 0;
}

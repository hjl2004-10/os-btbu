/* ch10: NPC世界主进程 */
/* 功能: 启动多个NPC进程，管理世界状态 */

#include <stdio.h>
#include <unistd.h>

/* ch10: NPC数量 */
#define NPC_COUNT 3

/* ch10: 简单的整数转字符串 */
static void int_to_str(int n, char *buf)
{
	int i = 0;
	char tmp[16];

	if (n == 0) {
		buf[0] = '0';
		buf[1] = '\0';
		return;
	}

	while (n > 0) {
		tmp[i++] = '0' + (n % 10);
		n /= 10;
	}

	int j = 0;
	while (i > 0) {
		buf[j++] = tmp[--i];
	}
	buf[j] = '\0';
}

int main(void)
{
	int pids[NPC_COUNT];
	int i;
	int code;

	printf("\n");
	printf("========================================\n");
	printf("ch10: NPC World Started\n");
	printf("ch10: Spawning %d NPCs...\n", NPC_COUNT);
	printf("========================================\n\n");

	/* ch10: 启动NPC进程 */
	for (i = 0; i < NPC_COUNT; i++) {
		int pid = fork();
		if (pid < 0) {
			printf("ch10: fork failed for NPC %d\n", i + 1);
			continue;
		}

		if (pid == 0) {
			/* ch10: 子进程 - 执行ch10_npc */
			char id_str[8];
			int_to_str(i + 1, id_str);
			char *argv[3];
			argv[0] = "ch10_npc";
			argv[1] = id_str;
			argv[2] = (char *)0;
			exec("ch10_npc", argv);
			/* exec失败 */
			printf("ch10: exec ch10_npc failed for NPC %d\n", i + 1);
			exit(-1);
		}

		/* ch10: 父进程记录子进程PID */
		pids[i] = pid;
		printf("ch10: NPC %d spawned (pid=%d)\n", i + 1, pid);
	}

	printf("\nch10: All NPCs spawned, entering world loop...\n\n");

	/* ch10: 等待所有NPC死亡 */
	int alive = NPC_COUNT;
	while (alive > 0) {
		int pid = waitpid(-1, &code);
		if (pid > 0) {
			/* ch10: 找到是哪个NPC */
			for (i = 0; i < NPC_COUNT; i++) {
				if (pids[i] == pid) {
					printf("ch10: NPC %d exited (pid=%d, code=%d)\n",
						i + 1, pid, code);
					pids[i] = -1;
					alive--;
					break;
				}
			}
		}
		sched_yield();
	}

	printf("\n========================================\n");
	printf("ch10: All NPCs dead, world ends\n");
	printf("========================================\n");

	return 0;
}

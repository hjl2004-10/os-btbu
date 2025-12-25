/* ch11: NPC世界主进程 */
/* 功能: 启动多个NPC进程，管理世界状态，创建NPC间通信管道 */

#include <stdio.h>
#include <unistd.h>

/* ch11: NPC数量 */
#define NPC_COUNT 3

/* ch11: 简单的整数转字符串 */
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
	printf("ch11: NPC Social World Started\n");
	printf("ch11: Four-thread architecture enabled\n");
	printf("ch11: Spawning %d NPCs...\n", NPC_COUNT);
	printf("========================================\n\n");

	/* ch11: 阶段二会在这里创建NPC间管道 */
	/* int pipes[NPC_COUNT][NPC_COUNT][2]; */
	/* for (i = 0; i < NPC_COUNT; i++) { */
	/*     for (j = 0; j < NPC_COUNT; j++) { */
	/*         if (i != j) pipe(pipes[i][j]); */
	/*     } */
	/* } */

	/* ch11: 启动NPC进程 */
	for (i = 0; i < NPC_COUNT; i++) {
		int pid = fork();
		if (pid < 0) {
			printf("ch11: fork failed for NPC %d\n", i + 1);
			continue;
		}

		if (pid == 0) {
			/* ch11: 子进程 - 执行ch11_npc */
			char id_str[8];
			int_to_str(i + 1, id_str);
			char *argv[3];
			argv[0] = "ch11_npc";
			argv[1] = id_str;
			argv[2] = (char *)0;
			exec("ch11_npc", argv);
			/* exec失败 */
			printf("ch11: exec ch11_npc failed for NPC %d\n", i + 1);
			exit(-1);
		}

		/* ch11: 父进程记录子进程PID */
		pids[i] = pid;
		printf("ch11: NPC %d spawned (pid=%d)\n", i + 1, pid);
	}

	printf("\nch11: All NPCs spawned, entering world loop...\n\n");

	/* ch11: 等待所有NPC死亡 */
	int alive = NPC_COUNT;
	while (alive > 0) {
		int pid = waitpid(-1, &code);
		if (pid > 0) {
			/* ch11: 找到是哪个NPC */
			for (i = 0; i < NPC_COUNT; i++) {
				if (pids[i] == pid) {
					printf("ch11: NPC %d exited (pid=%d, code=%d)\n",
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
	printf("ch11: All NPCs dead, world ends\n");
	printf("ch11: Social simulation completed\n");
	printf("========================================\n");

	return 0;
}

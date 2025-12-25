/* ch11: NPC世界主进程 */
/* 功能: 启动多个NPC进程，管理世界状态，创建NPC间通信管道 */

#include <stdio.h>
#include <unistd.h>

/* ch11: NPC数量 */
#define NPC_COUNT 3

/* ch11: 管道矩阵 pipes[from][to][0/1] */
/* pipes[i][j][0] = 读端, pipes[i][j][1] = 写端 */
/* NPC i 写入 pipes[i][j][1] 发送给 NPC j */
/* NPC j 读取 pipes[i][j][0] 接收来自 NPC i 的消息 */
static int pipes[NPC_COUNT][NPC_COUNT][2];

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
	int i, j;
	int code;

	printf("\n");
	printf("========================================\n");
	printf("ch11: NPC Social World Started\n");
	printf("ch11: Four-thread architecture + Pipe IPC\n");
	printf("ch11: Spawning %d NPCs...\n", NPC_COUNT);
	printf("========================================\n\n");

	/* ch11: 创建NPC间管道矩阵 */
	printf("ch11: Creating pipe matrix...\n");
	for (i = 0; i < NPC_COUNT; i++) {
		for (j = 0; j < NPC_COUNT; j++) {
			if (i != j) {
				int pfd[2];
				if (pipe(pfd) < 0) {
					printf("ch11: pipe(%d->%d) failed\n", i + 1, j + 1);
					pipes[i][j][0] = -1;
					pipes[i][j][1] = -1;
				} else {
					pipes[i][j][0] = pfd[0];
					pipes[i][j][1] = pfd[1];
					printf("ch11: pipe(%d->%d) created: r=%d, w=%d\n",
						i + 1, j + 1, pfd[0], pfd[1]);
				}
			} else {
				pipes[i][j][0] = -1;
				pipes[i][j][1] = -1;
			}
		}
	}

	/* ch11: 启动NPC进程 */
	for (i = 0; i < NPC_COUNT; i++) {
		int pid = fork();
		if (pid < 0) {
			printf("ch11: fork failed for NPC %d\n", i + 1);
			continue;
		}

		if (pid == 0) {
			/* ch11: 子进程 - 执行ch11_npc */
			/* ch11: 构建参数列表 */
			/* argv[0] = "ch11_npc" */
			/* argv[1] = npc_id (1-based) */
			/* argv[2..N+1] = read fds from other NPCs (N-1个) */
			/* argv[N+2..2N+1] = write fds to other NPCs (N-1个) */
			/* 格式: ch11_npc <id> <r0> <r1> ... <w0> <w1> ... */

			char id_str[8];
			int_to_str(i + 1, id_str);

			/* ch11: 收集这个NPC需要的fd */
			/* 读端: 其他NPC发给我的 pipes[j][i][0] */
			/* 写端: 我发给其他NPC的 pipes[i][j][1] */
			char fd_strs[2 * NPC_COUNT][8];
			int argc = 2;  /* argv[0]=name, argv[1]=id */

			/* ch11: 读端 - 接收来自其他NPC */
			for (j = 0; j < NPC_COUNT; j++) {
				if (j != i) {
					int_to_str(pipes[j][i][0], fd_strs[argc - 2]);
					argc++;
				}
			}

			/* ch11: 写端 - 发送给其他NPC */
			for (j = 0; j < NPC_COUNT; j++) {
				if (j != i) {
					int_to_str(pipes[i][j][1], fd_strs[argc - 2]);
					argc++;
				}
			}

			/* ch11: 构建argv数组 */
			char *argv[16];
			argv[0] = "ch11_npc";
			argv[1] = id_str;
			for (j = 0; j < argc - 2; j++) {
				argv[2 + j] = fd_strs[j];
			}
			argv[argc] = (char *)0;

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

	/* ch11: 关闭所有管道 */
	for (i = 0; i < NPC_COUNT; i++) {
		for (j = 0; j < NPC_COUNT; j++) {
			if (pipes[i][j][0] >= 0)
				close(pipes[i][j][0]);
			if (pipes[i][j][1] >= 0)
				close(pipes[i][j][1]);
		}
	}

	printf("\n========================================\n");
	printf("ch11: All NPCs dead, world ends\n");
	printf("ch11: Social simulation completed\n");
	printf("========================================\n");

	return 0;
}

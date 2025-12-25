/* ch11: NPC四线程架构 */
/* 功能: 感知、思考、沟通、记忆四线程并发运行 */
/* 阶段二: 真正使用管道进行NPC间通信 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ai_sched.h>

/* ch11: NPC数量 */
#define NPC_COUNT 3

/* ch11: 简单的字符串复制 */
static void str_copy(char *dst, const char *src, int maxlen)
{
	int i;
	for (i = 0; i < maxlen - 1 && src[i]; i++)
		dst[i] = src[i];
	dst[i] = '\0';
}

/* ch11: 简单的整数转字符串追加 */
static void append_int(char *buf, int n)
{
	char tmp[16];
	int i = 0, j;

	if (n == 0) {
		tmp[i++] = '0';
	} else {
		while (n > 0) {
			tmp[i++] = '0' + (n % 10);
			n /= 10;
		}
	}

	j = strlen(buf);
	while (i > 0)
		buf[j++] = tmp[--i];
	buf[j] = '\0';
}

/* ch11: 简单的字符串追加 */
static void str_append(char *dst, const char *src)
{
	int i = strlen(dst);
	int j = 0;
	while (src[j])
		dst[i++] = src[j++];
	dst[i] = '\0';
}

/* ch11: 简单的字符串转整数 */
static int str_to_int(const char *s)
{
	int n = 0;
	while (*s >= '0' && *s <= '9') {
		n = n * 10 + (*s - '0');
		s++;
	}
	return n;
}

/* ch11: NPC共享数据结构 */
struct npc_shared {
	/* ch11: 同步原语ID */
	int mutex_id;
	int cond_id;

	/* ch11: NPC基本信息 */
	int npc_id;
	int running;

	/* ch11: 管道fd数组 (阶段二新增) */
	/* read_fds[i] = 从NPC i+1读取的fd (跳过自己) */
	/* write_fds[i] = 写入NPC i+1的fd (跳过自己) */
	int read_fds[NPC_COUNT - 1];
	int write_fds[NPC_COUNT - 1];
	int read_from_npc[NPC_COUNT - 1];	/* read_fds[i]对应的NPC ID */
	int write_to_npc[NPC_COUNT - 1];	/* write_fds[i]对应的NPC ID */

	/* ch11: 感知缓冲区 - 收到的消息 */
	char inbox[256];
	int inbox_len;
	int inbox_from;		/* 发送者NPC ID, -1表示无消息 */

	/* ch11: 待发消息队列 */
	char outbox[256];
	int outbox_len;
	int outbox_to;		/* 目标NPC ID, -1表示不发送(to none) */

	/* ch11: 状态标志 */
	int has_new_msg;	/* 有新消息待处理 */
	int has_pending_send;	/* 有消息待发送 */
	int think_trigger;	/* 触发思考(收到消息或随机) */

	/* ch11: 统计 */
	int msg_sent;
	int msg_recv;
	int think_count;

	/* ch11: 目标进程PID (用于IPC奖励) */
	int target_pids[NPC_COUNT];	/* target_pids[npc_id-1] = pid */
};

/* ch11: 全局共享数据 */
static struct npc_shared g_shared;

/* ch11: 根据NPC ID找到写fd索引 */
static int find_write_fd_idx(struct npc_shared *shared, int target_npc)
{
	int i;
	for (i = 0; i < NPC_COUNT - 1; i++) {
		if (shared->write_to_npc[i] == target_npc)
			return i;
	}
	return -1;
}

/* ch11: 感知线程 - 从管道读取消息 */
static void perception_thread(void *arg)
{
	struct npc_shared *shared = (struct npc_shared *)arg;
	int loop = 0;
	int i;
	char buf[256];

	printf("[NPC %d][perception] Thread started\n", shared->npc_id);

	while (shared->running) {
		/* ch11: 轮询所有读管道 */
		for (i = 0; i < NPC_COUNT - 1 && shared->running; i++) {
			int fd = shared->read_fds[i];
			if (fd < 0)
				continue;

			/* ch11: 读取管道 - 当fd被关闭时read会返回错误 */
			int n = read(fd, buf, sizeof(buf) - 1);
			if (n > 0) {
				buf[n] = '\0';
				mutex_lock(shared->mutex_id);
				if (!shared->has_new_msg) {
					str_copy(shared->inbox, buf, sizeof(shared->inbox));
					shared->inbox_len = n;
					shared->inbox_from = shared->read_from_npc[i];
					shared->has_new_msg = 1;
					shared->msg_recv++;

					printf("[NPC %d][perception] Received from NPC %d: \"%s\"\n",
						shared->npc_id, shared->inbox_from, shared->inbox);

					/* ch11: 通知thinking线程 */
					condvar_signal(shared->cond_id);
				}
				mutex_unlock(shared->mutex_id);
			} else if (n < 0) {
				/* ch11: read错误(fd已关闭)，退出循环 */
				break;
			}
			/* ch11: n == 0 表示没有数据，继续轮询 */
		}

		/* ch11: 检查是否应该退出 */
		if (!shared->running)
			break;

		sched_yield();
		loop++;

		/* ch11: 限制循环次数 */
		if (loop > 100)
			break;
	}

	printf("[NPC %d][perception] Thread exiting\n", shared->npc_id);
	exit(0);
}

/* ch11: 思考线程 - 产生决策(阶段一用固定消息,阶段三调用AI) */
static void thinking_thread(void *arg)
{
	struct npc_shared *shared = (struct npc_shared *)arg;
	int loop = 0;

	printf("[NPC %d][thinking] Thread started\n", shared->npc_id);

	while (shared->running) {
		mutex_lock(shared->mutex_id);

		/* ch11: 检查是否需要思考 */
		int should_think = 0;

		/* 条件1: 收到新消息 */
		if (shared->has_new_msg) {
			should_think = 1;
			printf("[NPC %d][thinking] Triggered by new message\n",
				shared->npc_id);
		}

		/* 条件2: 随机主动发起 (约20%概率) */
		if (!should_think && (rand() % 100 < 20)) {
			should_think = 1;
			printf("[NPC %d][thinking] Triggered randomly (proactive)\n",
				shared->npc_id);
		}

		if (should_think && !shared->has_pending_send) {
			shared->think_count++;

			/* ch11: 阶段一使用固定响应，阶段三会调用AI */
			/* 决定发送目标: 如果有收到消息则回复，否则随机选一个 */
			if (shared->has_new_msg && shared->inbox_from > 0) {
				shared->outbox_to = shared->inbox_from;
				/* ch11: 构建回复消息 */
				shared->outbox[0] = '\0';
				str_append(shared->outbox, "Reply to NPC ");
				append_int(shared->outbox, shared->inbox_from);
				str_append(shared->outbox, ": Got your message!");
			} else {
				/* ch11: 随机选择目标或[to none] */
				int r = rand() % 4;
				if (r == 0) {
					/* 25%概率不发消息 */
					shared->outbox_to = -1;
					printf("[NPC %d][thinking] Decision: [to none]\n",
						shared->npc_id);
				} else {
					/* 选择一个其他NPC */
					shared->outbox_to = (r % 3) + 1;
					if (shared->outbox_to == shared->npc_id)
						shared->outbox_to = (shared->outbox_to % 3) + 1;
					/* ch11: 构建问候消息 */
					shared->outbox[0] = '\0';
					str_append(shared->outbox, "Hello NPC ");
					append_int(shared->outbox, shared->outbox_to);
					str_append(shared->outbox, "! How are you?");
				}
			}

			if (shared->outbox_to > 0) {
				shared->outbox_len = strlen(shared->outbox);
				shared->has_pending_send = 1;
				printf("[NPC %d][thinking] Decision: [to npc%d]: %s\n",
					shared->npc_id, shared->outbox_to, shared->outbox);
			}

			/* ch11: 清除已处理的消息 */
			shared->has_new_msg = 0;
			shared->inbox_from = -1;
		}

		mutex_unlock(shared->mutex_id);

		sched_yield();
		loop++;

		if (loop > 100)
			break;
	}

	printf("[NPC %d][thinking] Thread exiting (thought %d times)\n",
		shared->npc_id, shared->think_count);
	exit(0);
}

/* ch11: 沟通线程 - 通过管道发送消息 */
static void communication_thread(void *arg)
{
	struct npc_shared *shared = (struct npc_shared *)arg;
	int loop = 0;

	printf("[NPC %d][comm] Thread started\n", shared->npc_id);

	while (shared->running) {
		mutex_lock(shared->mutex_id);

		if (shared->has_pending_send && shared->outbox_to > 0) {
			/* ch11: 找到目标NPC的写fd */
			int idx = find_write_fd_idx(shared, shared->outbox_to);
			if (idx >= 0 && shared->write_fds[idx] >= 0) {
				/* ch11: 通过管道发送消息 */
				int n = write(shared->write_fds[idx],
					shared->outbox, shared->outbox_len);

				if (n > 0) {
					printf("[NPC %d][comm] Sent to NPC %d via pipe: \"%s\" (%d bytes)\n",
						shared->npc_id, shared->outbox_to, shared->outbox, n);

					/* ch11: 触发IPC奖励 */
					int target_pid = shared->target_pids[shared->outbox_to - 1];
					if (target_pid > 0) {
						npc_ipc_notify(target_pid, n);
						printf("[NPC %d][comm] IPC reward notified (target_pid=%d)\n",
							shared->npc_id, target_pid);
					}

					shared->msg_sent++;
				} else {
					printf("[NPC %d][comm] Write failed to NPC %d\n",
						shared->npc_id, shared->outbox_to);
				}
			} else {
				printf("[NPC %d][comm] No pipe to NPC %d\n",
					shared->npc_id, shared->outbox_to);
			}

			shared->has_pending_send = 0;
			shared->outbox_to = -1;
			shared->outbox_len = 0;
		} else if (shared->has_pending_send && shared->outbox_to < 0) {
			/* ch11: [to none] 不发送 */
			shared->has_pending_send = 0;
		}

		mutex_unlock(shared->mutex_id);

		sched_yield();
		loop++;

		if (loop > 100)
			break;
	}

	printf("[NPC %d][comm] Thread exiting (sent %d msgs)\n",
		shared->npc_id, shared->msg_sent);
	exit(0);
}

/* ch11: 记忆线程 - 管理三级记忆(阶段一框架,阶段三实现) */
static void memory_thread(void *arg)
{
	struct npc_shared *shared = (struct npc_shared *)arg;
	int loop = 0;

	printf("[NPC %d][memory] Thread started\n", shared->npc_id);

	/* ch11: 阶段一只是占位，阶段三会实现三级记忆 */
	/* L1: 人设 - 硬编码 */
	/* L2: AI记忆 - 内核存储 */
	/* L3: 会话历史 - 进程内存 */

	while (shared->running) {
		/* ch11: 阶段一暂时只是周期运行 */
		sched_yield();
		loop++;

		if (loop > 100)
			break;
	}

	printf("[NPC %d][memory] Thread exiting\n", shared->npc_id);
	exit(0);
}

int main(int argc, char *argv[])
{
	int npc_id;
	struct npc_status st;
	int t_percept, t_think, t_comm, t_memory;
	int loop_count = 0;
	int i, j;

	/* ch11: 解析参数 */
	/* argv[0] = "ch11_npc" */
	/* argv[1] = npc_id */
	/* argv[2..N] = read fds (N-1个) */
	/* argv[N+1..2N-1] = write fds (N-1个) */
	if (argc < 2) {
		printf("[NPC ?] Error: no NPC ID provided\n");
		return -1;
	}
	npc_id = str_to_int(argv[1]);

	printf("[NPC %d] Born! argc=%d\n", npc_id, argc);

	/* ch11: 初始化共享数据 */
	memset(&g_shared, 0, sizeof(g_shared));
	g_shared.npc_id = npc_id;
	g_shared.running = 1;
	g_shared.inbox_from = -1;
	g_shared.outbox_to = -1;

	/* ch11: 解析管道fd */
	/* 格式: ch11_npc <id> <r0> <r1> <w0> <w1> */
	/* 对于NPC 1: r0=从NPC2读, r1=从NPC3读, w0=写到NPC2, w1=写到NPC3 */
	int expected_argc = 2 + 2 * (NPC_COUNT - 1);  /* id + reads + writes */
	if (argc >= expected_argc) {
		int arg_idx = 2;
		int fd_idx = 0;

		/* ch11: 读fd - 来自其他NPC */
		for (i = 1; i <= NPC_COUNT; i++) {
			if (i != npc_id) {
				g_shared.read_fds[fd_idx] = str_to_int(argv[arg_idx]);
				g_shared.read_from_npc[fd_idx] = i;
				printf("[NPC %d] read_fd[%d]=%d (from NPC %d)\n",
					npc_id, fd_idx, g_shared.read_fds[fd_idx], i);
				fd_idx++;
				arg_idx++;
			}
		}

		/* ch11: 写fd - 发送给其他NPC */
		fd_idx = 0;
		for (i = 1; i <= NPC_COUNT; i++) {
			if (i != npc_id) {
				g_shared.write_fds[fd_idx] = str_to_int(argv[arg_idx]);
				g_shared.write_to_npc[fd_idx] = i;
				printf("[NPC %d] write_fd[%d]=%d (to NPC %d)\n",
					npc_id, fd_idx, g_shared.write_fds[fd_idx], i);
				fd_idx++;
				arg_idx++;
			}
		}
	} else {
		printf("[NPC %d] Warning: No pipe fds provided (argc=%d, expected=%d)\n",
			npc_id, argc, expected_argc);
		/* ch11: 没有管道时设置为-1 */
		for (i = 0; i < NPC_COUNT - 1; i++) {
			g_shared.read_fds[i] = -1;
			g_shared.write_fds[i] = -1;
		}
	}

	/* ch11: 初始化目标PID (简化: 假设按顺序fork，pid从3开始) */
	/* 实际应该通过world传递或查询 */
	for (i = 0; i < NPC_COUNT; i++) {
		g_shared.target_pids[i] = 3 + i;  /* NPC 1=pid 3, NPC 2=pid 4, ... */
	}

	printf("[NPC %d] Initializing 4-thread architecture...\n", npc_id);

	/* ch11: 初始化随机数种子 */
	srand(npc_id * 12345 + get_mtime());

	/* ch11: 向内核注册 */
	if (npc_register(npc_id) < 0) {
		printf("[NPC %d] Failed to register\n", npc_id);
		return -1;
	}

	/* ch11: 创建同步原语 */
	g_shared.mutex_id = mutex_blocking_create();
	if (g_shared.mutex_id < 0) {
		printf("[NPC %d] Failed to create mutex\n", npc_id);
		return -1;
	}

	g_shared.cond_id = condvar_create();
	if (g_shared.cond_id < 0) {
		printf("[NPC %d] Failed to create condvar\n", npc_id);
		return -1;
	}

	printf("[NPC %d] Sync primitives created: mutex=%d, cond=%d\n",
		npc_id, g_shared.mutex_id, g_shared.cond_id);

	/* ch11: 创建4个工作线程 */
	t_percept = thread_create(perception_thread, &g_shared);
	t_think = thread_create(thinking_thread, &g_shared);
	t_comm = thread_create(communication_thread, &g_shared);
	t_memory = thread_create(memory_thread, &g_shared);

	if (t_percept < 0 || t_think < 0 || t_comm < 0 || t_memory < 0) {
		printf("[NPC %d] Failed to create threads\n", npc_id);
		return -1;
	}

	printf("[NPC %d] 4 threads created: percept=%d, think=%d, comm=%d, memory=%d\n",
		npc_id, t_percept, t_think, t_comm, t_memory);

	/* ch11: 主循环 - 监控状态和触发进化调度 */
	while (1) {
		if (npc_get_status(&st) < 0) {
			printf("[NPC %d] Failed to get status\n", npc_id);
			break;
		}

		/* ch11: 每5轮打印状态 */
		if (loop_count % 5 == 0) {
			printf("[NPC %d] tick=%d, prio=%d, alive=%d, sent=%d, recv=%d\n",
				npc_id, st.tick, st.my_prio, st.alive_npcs,
				g_shared.msg_sent, g_shared.msg_recv);
		}

		/* ch11: 检查是否死亡 */
		if (st.my_prio <= 0) {
			printf("[NPC %d] I'm dying... (prio=%d)\n", npc_id, st.my_prio);
			g_shared.running = 0;
			/* ch11: 立即关闭管道，让阻塞的read()返回 */
			for (i = 0; i < NPC_COUNT - 1; i++) {
				if (g_shared.read_fds[i] >= 0) {
					close(g_shared.read_fds[i]);
					g_shared.read_fds[i] = -1;
				}
				if (g_shared.write_fds[i] >= 0) {
					close(g_shared.write_fds[i]);
					g_shared.write_fds[i] = -1;
				}
			}
			break;
		}

		/* ch11: 让出CPU，触发进化调度 */
		npc_yield();
		loop_count++;

		/* ch11: 限制最大轮数 */
		if (loop_count > 120) {
			printf("[NPC %d] Max loops reached\n", npc_id);
			g_shared.running = 0;
			/* ch11: 立即关闭管道 */
			for (i = 0; i < NPC_COUNT - 1; i++) {
				if (g_shared.read_fds[i] >= 0) {
					close(g_shared.read_fds[i]);
					g_shared.read_fds[i] = -1;
				}
				if (g_shared.write_fds[i] >= 0) {
					close(g_shared.write_fds[i]);
					g_shared.write_fds[i] = -1;
				}
			}
			break;
		}
	}

	/* ch11: 等待工作线程结束 */
	/* ch11: 注意：perception线程可能阻塞在read()上，无法正常退出 */
	/* ch11: 因为管道写端被其他进程持有，关闭自己的fd不会让read返回 */
	/* ch11: 简化处理：等待一小段时间后直接退出，不等perception线程 */
	printf("[NPC %d] Waiting for threads to exit...\n", npc_id);

	/* ch11: 先等待不会阻塞的线程 */
	waittid(t_think);
	waittid(t_comm);
	waittid(t_memory);

	/* ch11: perception线程可能阻塞，给它几轮调度机会 */
	for (i = 0; i < 5; i++) {
		sched_yield();
	}

	/* ch11: 不再等待perception线程，直接退出 */
	/* ch11: 进程退出时内核会清理所有线程 */

	printf("[NPC %d] Died after %d loops (sent=%d, recv=%d, think=%d)\n",
		npc_id, loop_count, g_shared.msg_sent, g_shared.msg_recv,
		g_shared.think_count);

	return 0;
}

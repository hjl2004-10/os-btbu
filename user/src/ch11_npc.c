/* ch11: NPC四线程架构 */
/* 功能: 感知、思考、沟通、记忆四线程并发运行 */
/* 阶段三: AI驱动决策 + 三级记忆系统 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ai_sched.h>

/* ch11: NPC数量 */
#define NPC_COUNT 3

/* ch11: 三级记忆系统参数 */
#define L2_MEMORY_SIZE 1024    /* L2记忆缓冲区大小 */
#define L3_HISTORY_SIZE 512    /* L3会话历史大小 */
#define AI_RESPONSE_SIZE 512   /* AI响应缓冲区大小 */
#define PROMPT_SIZE 1024       /* Prompt缓冲区大小 */

/* ch11: L1人设模板 - 每个NPC不同性格 */
static const char *L1_PERSONAS[NPC_COUNT] = {
	/* NPC 1: 外向热情型 */
	"你是NPC1，性格外向热情，喜欢主动和别人交流。"
	"说话简短友好(20字以内)。",
	/* NPC 2: 内向沉稳型 */
	"你是NPC2，性格内向沉稳，喜欢思考问题。"
	"说话简短有深度(20字以内)。",
	/* NPC 3: 幽默风趣型 */
	"你是NPC3，性格幽默风趣，喜欢开玩笑。"
	"说话简短有趣(20字以内)。"
};

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
	int ai_call_count;	/* AI调用次数 (阶段三) */

	/* ch11: 目标进程PID (用于IPC奖励) */
	int target_pids[NPC_COUNT];	/* target_pids[npc_id-1] = pid */

	/* ch11: 阶段三 - 三级记忆系统 */
	char l3_history[L3_HISTORY_SIZE];	/* L3: 会话历史 */
	int l3_len;
	char pending_memory[256];		/* 待保存到L2的记忆 */
	int pending_memory_len;
	int has_pending_memory;			/* 有记忆待保存 */
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

/* ch11: 阶段三 - 转小写 */
static char to_lower(char c)
{
	if (c >= 'A' && c <= 'Z')
		return c + ('a' - 'A');
	return c;
}

/* ch11: 阶段三 - 查找字符串(忽略大小写) */
static char *str_find_i(const char *haystack, const char *needle)
{
	if (!haystack || !needle || !*needle)
		return (char *)haystack;
	for (; *haystack; haystack++) {
		const char *h = haystack;
		const char *n = needle;
		while (*h && *n && to_lower(*h) == to_lower(*n)) {
			h++;
			n++;
		}
		if (!*n)
			return (char *)haystack;
	}
	return 0;
}

/* ch11: 阶段三 - 解析AI响应 */
/* 格式: [to npcX]: 消息内容 [memory]: 记忆内容 */
/* 支持大小写: [to NPC1] 或 [to npc1] 都可以 */
/* 返回: 目标NPC ID, -1表示[to none], 0表示解析失败 */
static int parse_ai_response(const char *response,
			     char *message, int msg_maxlen,
			     char *memory, int mem_maxlen)
{
	const char *p;
	int target_npc = 0;
	int i;

	message[0] = '\0';
	memory[0] = '\0';

	/* ch11: 查找 [to npcX] 或 [to none] (忽略大小写) */
	p = str_find_i(response, "[to ");
	if (p) {
		p += 4;  /* 跳过 "[to " */
		/* ch11: 检查 none (忽略大小写) */
		if (to_lower(p[0]) == 'n' && to_lower(p[1]) == 'o' &&
		    to_lower(p[2]) == 'n' && to_lower(p[3]) == 'e') {
			/* [to none] */
			target_npc = -1;
			p += 5;  /* 跳过 "none]" */
		} else if (to_lower(p[0]) == 'n' && to_lower(p[1]) == 'p' &&
			   to_lower(p[2]) == 'c') {
			/* [to npcX] 或 [to NPCX] */
			p += 3;  /* 跳过 "npc" 或 "NPC" */
			target_npc = 0;
			while (*p >= '0' && *p <= '9') {
				target_npc = target_npc * 10 + (*p - '0');
				p++;
			}
			if (*p == ']')
				p++;
		}

		/* ch11: 跳过冒号和空格 */
		while (*p == ':' || *p == ' ')
			p++;

		/* ch11: 提取消息内容到 [memory] 或末尾 */
		i = 0;
		while (*p && i < msg_maxlen - 1) {
			/* ch11: 遇到 [memory] 或 [to 停止 (忽略大小写) */
			if (p[0] == '[' && (to_lower(p[1]) == 'm' || to_lower(p[1]) == 't'))
				break;
			message[i++] = *p++;
		}
		/* ch11: 去除末尾空白 */
		while (i > 0 && (message[i-1] == ' ' || message[i-1] == '\n'))
			i--;
		message[i] = '\0';
	}

	/* ch11: 查找 [memory]: (忽略大小写) */
	p = str_find_i(response, "[memory]:");
	if (!p)
		p = str_find_i(response, "[memory] ");
	if (p) {
		p += 9;  /* 跳过 "[memory]:" 或 "[memory] " */
		while (*p == ' ')
			p++;
		i = 0;
		while (*p && i < mem_maxlen - 1 && *p != '[') {
			memory[i++] = *p++;
		}
		/* ch11: 去除末尾空白 */
		while (i > 0 && (memory[i-1] == ' ' || memory[i-1] == '\n'))
			i--;
		memory[i] = '\0';
	}

	return target_npc;
}

/* ch11: 阶段三 - 构建AI prompt */
/* reply_to: 回复目标NPC ID, 0表示主动发起聊天 */
static int build_ai_prompt(struct npc_shared *shared, char *prompt, int maxlen,
			   const char *situation, int reply_to)
{
	char l2_memory[L2_MEMORY_SIZE];
	int len = 0;

	prompt[0] = '\0';

	/* ch11: L1人设 */
	str_append(prompt, L1_PERSONAS[shared->npc_id - 1]);
	str_append(prompt, "\n");

	/* ch11: L2记忆 - 从内核加载 */
	if (npc_memory_load(shared->npc_id, l2_memory, sizeof(l2_memory)) > 0) {
		str_append(prompt, "你的记忆: ");
		str_append(prompt, l2_memory);
		str_append(prompt, "\n");
	}

	/* ch11: L3会话历史 - 只显示与当前对话者相关的 */
	if (shared->l3_len > 0) {
		str_append(prompt, "最近对话: ");
		str_append(prompt, shared->l3_history);
		str_append(prompt, "\n");
	}

	/* ch11: 当前情况 */
	str_append(prompt, "现在情况: ");
	str_append(prompt, situation);
	str_append(prompt, "\n");

	/* ch11: 输出格式要求 - 根据reply_to区分回复和主动发起 */
	if (reply_to > 0) {
		/* ch11: 收到消息，必须回复发送者 */
		str_append(prompt, "你必须回复NPC");
		append_int(prompt, reply_to);
		str_append(prompt, "，格式:\n");
		str_append(prompt, "[to npc");
		append_int(prompt, reply_to);
		str_append(prompt, "]: 你的回复\n");
		str_append(prompt, "或者 [to none]: 不回复\n");
	} else {
		/* ch11: 主动发起聊天，可选择目标 */
		str_append(prompt, "请用以下格式回复(只回复一行):\n");
		str_append(prompt, "[to npc数字]: 你要说的话\n");
		str_append(prompt, "或者 [to none]: 不说话\n");
	}
	str_append(prompt, "可选: [memory]: 你想记住的事\n");

	return strlen(prompt);
}

/* ch11: 阶段三 - 追加到L3会话历史 */
static void append_to_l3(struct npc_shared *shared, const char *entry)
{
	int entry_len = strlen(entry);
	int remain = L3_HISTORY_SIZE - shared->l3_len - 2;

	if (remain < entry_len) {
		/* ch11: 空间不足，清除旧历史 */
		shared->l3_len = 0;
		shared->l3_history[0] = '\0';
		remain = L3_HISTORY_SIZE - 2;
	}

	if (shared->l3_len > 0) {
		shared->l3_history[shared->l3_len++] = ';';
	}

	int copy_len = (entry_len < remain) ? entry_len : remain;
	for (int i = 0; i < copy_len; i++) {
		shared->l3_history[shared->l3_len++] = entry[i];
	}
	shared->l3_history[shared->l3_len] = '\0';
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

/* ch11: 思考线程 - 阶段三: AI驱动决策 */
static void thinking_thread(void *arg)
{
	struct npc_shared *shared = (struct npc_shared *)arg;
	int loop = 0;
	char prompt[PROMPT_SIZE];
	char ai_response[AI_RESPONSE_SIZE];
	char message[256];
	char memory[256];
	char situation[128];
	int target_npc;

	printf("[NPC %d][thinking] Thread started (AI-driven)\n", shared->npc_id);

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

			/* ch11: 阶段三 - 构建当前情况描述 */
			/* ch11: reply_to = 回复目标, 0表示主动发起 */
			int reply_to = 0;
			situation[0] = '\0';
			if (shared->has_new_msg && shared->inbox_from > 0) {
				/* ch11: 收到消息，需要回复发送者 */
				reply_to = shared->inbox_from;
				str_append(situation, "NPC");
				append_int(situation, shared->inbox_from);
				str_append(situation, "对你说: ");
				str_append(situation, shared->inbox);

				/* ch11: 记录到L3会话历史 */
				char l3_entry[128];
				l3_entry[0] = '\0';
				str_append(l3_entry, "NPC");
				append_int(l3_entry, shared->inbox_from);
				str_append(l3_entry, ":");
				str_append(l3_entry, shared->inbox);
				append_to_l3(shared, l3_entry);
			} else {
				/* ch11: 主动发起，reply_to保持为0 */
				str_append(situation, "你想主动找人聊天。可选目标: ");
				int first = 1;
				for (int j = 1; j <= NPC_COUNT; j++) {
					if (j != shared->npc_id) {
						if (!first) str_append(situation, ",");
						str_append(situation, "NPC");
						append_int(situation, j);
						first = 0;
					}
				}
			}

			/* ch11: 阶段三 - 构建AI prompt，传入reply_to */
			build_ai_prompt(shared, prompt, sizeof(prompt), situation, reply_to);

			printf("[NPC %d][thinking] Calling AI...\n", shared->npc_id);

			/* ch11: 释放锁后调用AI (AI调用可能耗时较长) */
			mutex_unlock(shared->mutex_id);

			/* ch11: 调用AI API */
			int ai_ret = npc_ai_chat(prompt, ai_response, sizeof(ai_response));

			mutex_lock(shared->mutex_id);

			if (ai_ret > 0) {
				shared->ai_call_count++;
				printf("[NPC %d][thinking] AI response: %s\n",
					shared->npc_id, ai_response);

				/* ch11: 解析AI响应 */
				target_npc = parse_ai_response(ai_response,
					message, sizeof(message),
					memory, sizeof(memory));

				if (target_npc != 0) {
					shared->outbox_to = target_npc;
					if (target_npc > 0 && strlen(message) > 0) {
						str_copy(shared->outbox, message, sizeof(shared->outbox));
						shared->outbox_len = strlen(shared->outbox);
						shared->has_pending_send = 1;
						printf("[NPC %d][thinking] Decision: [to npc%d]: %s\n",
							shared->npc_id, target_npc, shared->outbox);

						/* ch11: 记录自己的发言到L3 */
						char l3_entry[128];
						l3_entry[0] = '\0';
						str_append(l3_entry, "我->NPC");
						append_int(l3_entry, target_npc);
						str_append(l3_entry, ":");
						str_append(l3_entry, message);
						append_to_l3(shared, l3_entry);
					} else if (target_npc < 0) {
						printf("[NPC %d][thinking] Decision: [to none]\n",
							shared->npc_id);
					}

					/* ch11: 如果有记忆需要保存 */
					if (strlen(memory) > 0) {
						str_copy(shared->pending_memory, memory,
							sizeof(shared->pending_memory));
						shared->pending_memory_len = strlen(memory);
						shared->has_pending_memory = 1;
						printf("[NPC %d][thinking] Memory to save: %s\n",
							shared->npc_id, memory);
					}
				} else {
					printf("[NPC %d][thinking] AI response parse failed\n",
						shared->npc_id);
				}
			} else {
				printf("[NPC %d][thinking] AI call failed (ret=%d)\n",
					shared->npc_id, ai_ret);
				/* ch11: AI失败时使用简单回退逻辑 */
				if (shared->has_new_msg && shared->inbox_from > 0) {
					shared->outbox_to = shared->inbox_from;
					shared->outbox[0] = '\0';
					str_append(shared->outbox, "收到!");
					shared->outbox_len = strlen(shared->outbox);
					shared->has_pending_send = 1;
				}
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

	printf("[NPC %d][thinking] Thread exiting (thought %d times, AI calls %d)\n",
		shared->npc_id, shared->think_count, shared->ai_call_count);
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

/* ch11: 记忆线程 - 阶段三: 管理三级记忆 */
static void memory_thread(void *arg)
{
	struct npc_shared *shared = (struct npc_shared *)arg;
	int loop = 0;
	int save_count = 0;

	printf("[NPC %d][memory] Thread started (L2 persistence)\n", shared->npc_id);

	/* ch11: 三级记忆说明 */
	/* L1: 人设 - 硬编码在L1_PERSONAS数组 */
	/* L2: AI记忆 - 内核存储, 通过npc_memory_save/load访问 */
	/* L3: 会话历史 - 进程内存, 在shared->l3_history */

	while (shared->running) {
		mutex_lock(shared->mutex_id);

		/* ch11: 检查是否有待保存的记忆 */
		if (shared->has_pending_memory && shared->pending_memory_len > 0) {
			char *mem_content = shared->pending_memory;
			int mem_len = shared->pending_memory_len;

			printf("[NPC %d][memory] Saving to L2: \"%s\"\n",
				shared->npc_id, mem_content);

			/* ch11: 保存到内核L2记忆区 */
			int ret = npc_memory_save(shared->npc_id, mem_content, mem_len);
			if (ret >= 0) {
				save_count++;
				printf("[NPC %d][memory] L2 save OK (%d bytes)\n",
					shared->npc_id, ret);
			} else {
				printf("[NPC %d][memory] L2 save failed\n", shared->npc_id);
			}

			/* ch11: 清除待保存标志 */
			shared->has_pending_memory = 0;
			shared->pending_memory_len = 0;
		}

		mutex_unlock(shared->mutex_id);

		sched_yield();
		loop++;

		if (loop > 100)
			break;
	}

	printf("[NPC %d][memory] Thread exiting (saved %d memories)\n",
		shared->npc_id, save_count);
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

	printf("[NPC %d] Born! argc=%d (AI-driven, 3-level memory)\n", npc_id, argc);

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

	printf("[NPC %d] Died after %d loops (sent=%d, recv=%d, think=%d, ai=%d)\n",
		npc_id, loop_count, g_shared.msg_sent, g_shared.msg_recv,
		g_shared.think_count, g_shared.ai_call_count);

	return 0;
}

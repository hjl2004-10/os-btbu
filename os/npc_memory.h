/* ch11: NPC记忆管理头文件 */
#ifndef NPC_MEMORY_H
#define NPC_MEMORY_H

#include "types.h"

/* ch11: 记忆系统参数 */
#define NPC_MEMORY_MAX      8       /* 最大NPC数量 */
#define NPC_MEMORY_L2_SIZE  2048    /* L2记忆区大小 (字节) */

/* ch11: NPC记忆结构 */
struct npc_memory {
    int npc_id;                         /* NPC编号, -1表示未使用 */
    char l2_memory[NPC_MEMORY_L2_SIZE]; /* L2记忆内容 */
    int l2_len;                         /* L2记忆长度 */
};

/* ch11: 记忆管理函数 */
void npc_memory_init(void);                                     /* 初始化记忆系统 */
int npc_memory_save(int npc_id, const char *content, int len);  /* 保存L2记忆 */
int npc_memory_load(int npc_id, char *buf, int maxlen);         /* 读取L2记忆 */
int npc_memory_clear(int npc_id);                               /* 清除记忆 */

#endif /* NPC_MEMORY_H */

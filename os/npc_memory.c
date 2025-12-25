/* ch11: NPC记忆管理实现 */
#include "npc_memory.h"
#include "defs.h"

/* ch11: 全局记忆存储 */
static struct npc_memory g_memories[NPC_MEMORY_MAX];

/* ch11: 初始化记忆系统 */
void npc_memory_init(void)
{
    int i;
    for (i = 0; i < NPC_MEMORY_MAX; i++) {
        g_memories[i].npc_id = -1;
        g_memories[i].l2_len = 0;
    }
    infof("ch11: npc_memory initialized (%d slots)", NPC_MEMORY_MAX);
}

/* ch11: 查找或分配NPC记忆槽 */
static struct npc_memory *find_or_alloc_memory(int npc_id)
{
    int i;
    int free_slot = -1;

    /* ch11: 先查找已存在的 */
    for (i = 0; i < NPC_MEMORY_MAX; i++) {
        if (g_memories[i].npc_id == npc_id)
            return &g_memories[i];
        if (g_memories[i].npc_id < 0 && free_slot < 0)
            free_slot = i;
    }

    /* ch11: 分配新槽 */
    if (free_slot >= 0) {
        g_memories[free_slot].npc_id = npc_id;
        g_memories[free_slot].l2_len = 0;
        return &g_memories[free_slot];
    }

    return NULL;
}

/* ch11: 查找NPC记忆槽 (不分配) */
static struct npc_memory *find_memory(int npc_id)
{
    int i;
    for (i = 0; i < NPC_MEMORY_MAX; i++) {
        if (g_memories[i].npc_id == npc_id)
            return &g_memories[i];
    }
    return NULL;
}

/* ch11: 保存L2记忆 - 追加模式 */
int npc_memory_save(int npc_id, const char *content, int len)
{
    struct npc_memory *mem;

    if (npc_id < 0 || content == NULL || len <= 0)
        return -1;

    mem = find_or_alloc_memory(npc_id);
    if (mem == NULL) {
        errorf("ch11: no memory slot for NPC %d", npc_id);
        return -1;
    }

    /* ch11: 检查是否有空间追加 */
    int remain = NPC_MEMORY_L2_SIZE - mem->l2_len - 1;  /* 预留\0 */
    if (remain <= 0) {
        debugf("ch11: NPC %d memory full, truncating old", npc_id);
        /* ch11: 简单策略 - 清空旧记忆 */
        mem->l2_len = 0;
        remain = NPC_MEMORY_L2_SIZE - 1;
    }

    /* ch11: 追加新记忆 */
    int copy_len = (len < remain) ? len : remain;

    /* ch11: 添加换行分隔 */
    if (mem->l2_len > 0 && mem->l2_len < NPC_MEMORY_L2_SIZE - 1) {
        mem->l2_memory[mem->l2_len++] = '\n';
        remain--;
        if (copy_len > remain) copy_len = remain;
    }

    /* ch11: 复制内容 */
    for (int i = 0; i < copy_len; i++) {
        mem->l2_memory[mem->l2_len + i] = content[i];
    }
    mem->l2_len += copy_len;
    mem->l2_memory[mem->l2_len] = '\0';

    debugf("ch11: NPC %d saved %d bytes to L2 (total=%d)",
           npc_id, copy_len, mem->l2_len);

    return copy_len;
}

/* ch11: 读取L2记忆 */
int npc_memory_load(int npc_id, char *buf, int maxlen)
{
    struct npc_memory *mem;

    if (npc_id < 0 || buf == NULL || maxlen <= 0)
        return -1;

    mem = find_memory(npc_id);
    if (mem == NULL) {
        /* ch11: 没有记忆，返回空字符串 */
        buf[0] = '\0';
        return 0;
    }

    /* ch11: 复制记忆内容 */
    int copy_len = (mem->l2_len < maxlen - 1) ? mem->l2_len : (maxlen - 1);
    for (int i = 0; i < copy_len; i++) {
        buf[i] = mem->l2_memory[i];
    }
    buf[copy_len] = '\0';

    debugf("ch11: NPC %d loaded %d bytes from L2", npc_id, copy_len);

    return copy_len;
}

/* ch11: 清除NPC记忆 */
int npc_memory_clear(int npc_id)
{
    struct npc_memory *mem = find_memory(npc_id);
    if (mem == NULL)
        return -1;

    mem->npc_id = -1;
    mem->l2_len = 0;
    debugf("ch11: NPC %d memory cleared", npc_id);

    return 0;
}

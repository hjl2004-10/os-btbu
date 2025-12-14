/* ch10: 进化调度器实现 */
#include "ai_sched.h"
#include "defs.h"
#include "proc.h"

/* ch10: 全局调度状态 */
static int g_world_tick = 0;            /* 世界时钟 */
static int g_alive_npcs = 0;            /* 存活NPC数 */

/* ch10: 初始化进化调度器 */
void ai_sched_init(void)
{
    g_world_tick = 0;
    g_alive_npcs = 0;
    infof("ch10: ai_sched initialized");
}

/* ch10: 调度tick - 优先级衰减 + 淘汰检查 */
void ai_sched_tick(void)
{
    extern struct proc pool[];
    int alive = 0;

    g_world_tick++;

    /* ch10: 遍历所有进程 */
    for (int i = 0; i < NPROC; i++) {
        struct proc *p = &pool[i];

        /* ch10: 只处理NPC进程 */
        if (p->state != P_USED || p->npc_id < 0)
            continue;

        /* ch10: 跳过已死亡的NPC */
        if (p->dynamic_prio < 0)
            continue;

        alive++;

        /* ch10: 优先级衰减 (新陈代谢) */
        p->dynamic_prio -= NPC_DECAY_RATE;

        /* ch10: IPC奖励结算 - 每64字节奖励1点优先级 */
        if (p->ipc_rx_bytes > 0) {
            int reward = p->ipc_rx_bytes / NPC_IPC_REWARD_BYTES;
            if (reward > 0) {
                p->dynamic_prio += reward;
                debugf("ch10: NPC %d got %d prio from %d bytes IPC",
                       p->npc_id, reward, (int)p->ipc_rx_bytes);
            }
            p->ipc_rx_bytes = 0;
        }

        /* ch10: 优先级上限 */
        if (p->dynamic_prio > NPC_PRIO_MAX)
            p->dynamic_prio = NPC_PRIO_MAX;

        /* ch10: 死亡检查 */
        if (p->dynamic_prio <= NPC_DEATH_THRESHOLD) {
            printf("ch10: [OOM Killer] NPC %d died (prio=%d)\n",
                   p->npc_id, p->dynamic_prio);
            /* ch10: 标记进程死亡，让其自然退出 */
            p->dynamic_prio = -1;  /* 标记为已死亡 */
        }
    }

    g_alive_npcs = alive;

    /* ch10: 每10个tick打印一次状态 */
    if (g_world_tick % 10 == 0) {
        debugf("ch10: tick=%d, alive_npcs=%d", g_world_tick, g_alive_npcs);
    }
}

/* ch10: 注册为NPC进程 */
int ai_sched_register(int npc_id)
{
    struct proc *p = curr_proc();

    if (npc_id < 0 || npc_id >= NPC_MAX_COUNT) {
        errorf("ch10: invalid npc_id %d", npc_id);
        return -1;
    }

    p->npc_id = npc_id;
    p->dynamic_prio = NPC_INIT_PRIO;
    p->ipc_rx_bytes = 0;

    g_alive_npcs++;

    printf("ch10: NPC %d registered (pid=%d, prio=%d)\n",
           npc_id, p->pid, p->dynamic_prio);

    return 0;
}

/* ch10: 获取NPC状态 */
int ai_sched_get_status(struct npc_status *st)
{
    struct proc *p = curr_proc();

    if (st == NULL)
        return -1;

    st->npc_id = p->npc_id;
    st->my_prio = p->dynamic_prio;
    st->my_memory = p->max_page;
    st->world_load = g_alive_npcs;
    st->alive_npcs = g_alive_npcs;
    st->tick = g_world_tick;

    return 0;
}

/* ch10: 增加IPC流量 (被其他进程调用) */
void ai_sched_add_ipc(int pid, uint64 bytes)
{
    extern struct proc pool[];

    /* ch10: 查找目标进程 */
    for (int i = 0; i < NPROC; i++) {
        struct proc *p = &pool[i];
        if (p->state == P_USED && p->pid == pid && p->npc_id >= 0) {
            p->ipc_rx_bytes += bytes;
            debugf("ch10: NPC %d received %d bytes IPC", p->npc_id, bytes);
            return;
        }
    }
}

/* ch10: 获取存活NPC数量 */
int ai_sched_get_alive_count(void)
{
    return g_alive_npcs;
}

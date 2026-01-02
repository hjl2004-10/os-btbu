# 第八章：并发与死锁检测

## 一、任务目标

本章需要实现两部分内容：

1. **同步原语**：mutex（互斥锁）、semaphore（信号量）、condvar（条件变量）
2. **死锁检测**：基于银行家算法的死锁检测机制

## 二、探索过程

### 2.1 理解并发问题

在多线程环境下，多个线程可能同时访问共享资源，导致竞态条件。例如：

```
线程A: counter = counter + 1
线程B: counter = counter + 1

执行顺序可能是：
1. A 读取 counter = 0
2. B 读取 counter = 0
3. A 写入 counter = 1
4. B 写入 counter = 1

结果：counter = 1，而非预期的 2
```

我们需要同步原语来保护临界区。

### 2.2 三种同步原语的区别

| 原语 | 特点 | 使用场景 |
|------|------|----------|
| **Mutex** | 二元锁（0或1）| 保护临界区，同一时间只有一个线程能进入 |
| **Semaphore** | 计数信号量（0~N）| 控制对有限资源的访问，如连接池 |
| **Condvar** | 条件变量 | 等待某个条件成立，需配合 mutex 使用 |

### 2.3 Mutex 的两种模式

我们的实现支持两种互斥锁：

**自旋锁 (Spin Mutex)**：
- 获取失败时**忙等待**（循环检查）
- 适用于锁持有时间很短的场景
- 不让出 CPU，避免上下文切换开销

**阻塞锁 (Blocking Mutex)**：
- 获取失败时**阻塞等待**（进入等待队列，让出 CPU）
- 适用于锁持有时间较长的场景
- 等待时不占用 CPU

### 2.4 理解死锁

**死锁的四个必要条件**：
1. **互斥**：资源一次只能被一个线程持有
2. **持有并等待**：线程持有资源的同时等待其他资源
3. **不可抢占**：资源只能由持有者主动释放
4. **循环等待**：存在线程的循环等待链

**死锁示例**：
```
线程A: 持有 mutex1，等待 mutex2
线程B: 持有 mutex2，等待 mutex1
→ 两个线程永远等待，形成死锁
```

### 2.5 银行家算法

银行家算法用于**死锁避免**，在分配资源前检测是否会导致不安全状态。

**核心数据结构**：
- `Available[m]`：系统当前可用的各类资源数量
- `Allocation[n][m]`：每个线程已分配的资源
- `Request[n][m]`：每个线程正在请求的资源

**算法流程**：
```
1. Work = Available, Finish[i] = false (for all i)

2. 找到一个线程 i 满足:
   - Finish[i] == false
   - Request[i] <= Work

3. 如果找到:
   - Work = Work + Allocation[i]  // 假设 i 运行完毕释放资源
   - Finish[i] = true
   - 回到步骤 2

4. 如果所有 Finish[i] == true，则安全；否则死锁
```

**直觉理解**：模拟"最好情况"——假设每个能运行的线程都会运行完毕并释放资源。如果这样都无法让所有线程完成，就存在死锁。

## 三、代码实现

### 3.1 同步原语的数据结构

```c
// os/sync.h

#define WAIT_QUEUE_MAX_LENGTH 16

struct mutex {
    uint blocking;    // 是否为阻塞锁
    uint locked;      // 是否被锁定
    struct queue wait_queue;  // 等待队列（阻塞锁用）
    int _wait_queue_data[WAIT_QUEUE_MAX_LENGTH];
};

struct semaphore {
    int count;        // 信号量计数
    struct queue wait_queue;  // 等待队列
    int _wait_queue_data[WAIT_QUEUE_MAX_LENGTH];
};

struct condvar {
    struct queue wait_queue;  // 等待队列
    int _wait_queue_data[WAIT_QUEUE_MAX_LENGTH];
};
```

### 3.2 进程中的同步资源池

```c
// os/proc.h

struct proc {
    // ... 其他字段 ...

    // 同步原语池
    uint next_mutex_id, next_semaphore_id, next_condvar_id;
    struct mutex mutex_pool[LOCK_POOL_SIZE];
    struct semaphore semaphore_pool[LOCK_POOL_SIZE];
    struct condvar condvar_pool[LOCK_POOL_SIZE];

    // 死锁检测相关
    int deadlock_detect_enable;
    int mutex_allocation[NTHREAD][LOCK_POOL_SIZE];  // 分配矩阵
    int mutex_request[NTHREAD][LOCK_POOL_SIZE];     // 请求矩阵
    int sem_allocation[NTHREAD][LOCK_POOL_SIZE];
    int sem_request[NTHREAD][LOCK_POOL_SIZE];
};
```

### 3.3 Mutex 实现

```c
// os/sync.c

struct mutex *mutex_create(int blocking)
{
    struct proc *p = curr_proc();
    if (p->next_mutex_id >= LOCK_POOL_SIZE)
        return NULL;

    struct mutex *m = &p->mutex_pool[p->next_mutex_id];
    p->next_mutex_id++;
    m->blocking = blocking;
    m->locked = 0;

    if (blocking) {
        // 阻塞锁需要等待队列
        init_queue(&m->wait_queue, WAIT_QUEUE_MAX_LENGTH,
                   m->_wait_queue_data);
    }
    return m;
}

void mutex_lock(struct mutex *m)
{
    int id = m - curr_proc()->mutex_pool;
    struct proc *p = curr_proc();
    struct thread *t = curr_thread();

    if (!m->locked) {
        // 锁空闲，直接获取
        m->locked = 1;
        p->mutex_allocation[t->tid][id] = 1;
        return;
    }

    if (!m->blocking) {
        // 自旋锁：忙等待
        p->mutex_request[t->tid][id] = 1;
        while (m->locked) {
            yield();  // 让出 CPU 但不阻塞
        }
        p->mutex_request[t->tid][id] = 0;
        p->mutex_allocation[t->tid][id] = 1;
        return;
    }

    // 阻塞锁：进入等待队列
    push_queue(&m->wait_queue, task_to_id(t));
    t->state = SLEEPING;
    p->mutex_request[t->tid][id] = 1;
    sched();  // 切换到其他线程
    // 被唤醒，获得锁
    p->mutex_request[t->tid][id] = 0;
    p->mutex_allocation[t->tid][id] = 1;
}

void mutex_unlock(struct mutex *m)
{
    int id = m - curr_proc()->mutex_pool;
    struct proc *p = curr_proc();

    if (m->blocking) {
        struct thread *t = id_to_task(pop_queue(&m->wait_queue));
        if (t == NULL) {
            // 没有等待者，直接释放
            m->locked = 0;
            p->mutex_allocation[curr_thread()->tid][id] = 0;
        } else {
            // 将锁传递给等待者
            t->state = RUNNABLE;
            add_task(t);
            p->mutex_allocation[curr_thread()->tid][id] = 0;
            p->mutex_request[t->tid][id] = 0;
            p->mutex_allocation[t->tid][id] = 1;
        }
    } else {
        m->locked = 0;
        p->mutex_allocation[curr_thread()->tid][id] = 0;
    }
}
```

### 3.4 Semaphore 实现

```c
// os/sync.c

struct semaphore *semaphore_create(int count)
{
    struct proc *p = curr_proc();
    if (p->next_semaphore_id >= LOCK_POOL_SIZE)
        return NULL;

    struct semaphore *s = &p->semaphore_pool[p->next_semaphore_id];
    p->next_semaphore_id++;
    s->count = count;
    init_queue(&s->wait_queue, WAIT_QUEUE_MAX_LENGTH, s->_wait_queue_data);
    return s;
}

void semaphore_up(struct semaphore *s)
{
    int id = s - curr_proc()->semaphore_pool;
    struct proc *p = curr_proc();
    struct thread *t = curr_thread();

    // 释放资源
    if (p->sem_allocation[t->tid][id] > 0)
        p->sem_allocation[t->tid][id]--;

    s->count++;
    if (s->count <= 0) {
        // 有等待者，唤醒一个
        struct thread *t = id_to_task(pop_queue(&s->wait_queue));
        if (t == NULL)
            panic("count <= 0 after up but wait queue is empty?");
        t->state = RUNNABLE;
        add_task(t);
        p->sem_request[t->tid][id] = 0;
        p->sem_allocation[t->tid][id]++;
    }
}

void semaphore_down(struct semaphore *s)
{
    int id = s - curr_proc()->semaphore_pool;
    struct proc *p = curr_proc();
    struct thread *t = curr_thread();

    s->count--;
    if (s->count < 0) {
        // 资源不足，等待
        push_queue(&s->wait_queue, task_to_id(t));
        t->state = SLEEPING;
        p->sem_request[t->tid][id] = 1;
        sched();
        // 被唤醒，获得资源
        p->sem_request[t->tid][id] = 0;
        p->sem_allocation[t->tid][id]++;
    } else {
        // 直接获得资源
        p->sem_allocation[t->tid][id]++;
    }
}
```

### 3.5 条件变量实现

```c
// os/sync.c

struct condvar *condvar_create()
{
    struct proc *p = curr_proc();
    if (p->next_condvar_id >= LOCK_POOL_SIZE)
        return NULL;

    struct condvar *c = &p->condvar_pool[p->next_condvar_id];
    p->next_condvar_id++;
    init_queue(&c->wait_queue, WAIT_QUEUE_MAX_LENGTH, c->_wait_queue_data);
    return c;
}

void cond_signal(struct condvar *cond)
{
    struct thread *t = id_to_task(pop_queue(&cond->wait_queue));
    if (t) {
        t->state = RUNNABLE;
        add_task(t);
    }
}

void cond_wait(struct condvar *cond, struct mutex *m)
{
    // 1. 先释放 mutex（避免死锁）
    mutex_unlock(m);

    // 2. 进入条件变量的等待队列
    struct thread *t = curr_thread();
    push_queue(&cond->wait_queue, task_to_id(t));
    t->state = SLEEPING;
    sched();

    // 3. 被唤醒后重新获取 mutex
    mutex_lock(m);
}
```

### 3.6 死锁检测算法

```c
// os/syscall.c

int deadlock_detect(struct proc *p)
{
    int work_mutex[LOCK_POOL_SIZE];
    int work_sem[LOCK_POOL_SIZE];
    int finish[NTHREAD];
    int i, j;

    // 初始化 Work = Available
    for (i = 0; i < LOCK_POOL_SIZE; i++) {
        work_mutex[i] = p->mutex_pool[i].locked ? 0 : 1;
        work_sem[i] = p->semaphore_pool[i].count > 0 ?
                      p->semaphore_pool[i].count : 0;
    }

    // 初始化 Finish
    for (i = 0; i < NTHREAD; i++) {
        finish[i] = (p->threads[i].state == T_UNUSED ||
                     p->threads[i].state == EXITED) ? 1 : 0;
    }

    // 银行家算法主循环
    while (1) {
        int found = 0;
        for (i = 0; i < NTHREAD; i++) {
            if (!finish[i]) {
                int possible = 1;

                // 检查 mutex 需求 Request <= Work
                for (j = 0; j < LOCK_POOL_SIZE; j++) {
                    if (p->mutex_request[i][j] > work_mutex[j]) {
                        possible = 0;
                        break;
                    }
                }
                if (!possible) continue;

                // 检查 semaphore 需求
                for (j = 0; j < LOCK_POOL_SIZE; j++) {
                    if (p->sem_request[i][j] > work_sem[j]) {
                        possible = 0;
                        break;
                    }
                }

                if (possible) {
                    // 假设线程 i 完成，释放资源
                    // Work = Work + Allocation
                    for (j = 0; j < LOCK_POOL_SIZE; j++) {
                        work_mutex[j] += p->mutex_allocation[i][j];
                        work_sem[j] += p->sem_allocation[i][j];
                    }
                    finish[i] = 1;
                    found = 1;
                }
            }
        }
        if (!found) break;
    }

    // 检查是否所有线程都能完成
    for (i = 0; i < NTHREAD; i++) {
        if (!finish[i])
            return 1;  // 发现死锁
    }
    return 0;
}
```

### 3.7 在 mutex_lock/semaphore_down 中集成死锁检测

```c
// os/syscall.c

int sys_mutex_lock(int mutex_id)
{
    if (mutex_id < 0 || mutex_id >= curr_proc()->next_mutex_id)
        return -1;

    struct proc *p = curr_proc();
    int tid = curr_thread()->tid;

    if (p->deadlock_detect_enable) {
        // 尝试申请资源
        p->mutex_request[tid][mutex_id] = 1;

        // 检测死锁
        if (deadlock_detect(p)) {
            // 死锁！撤销请求并返回错误
            p->mutex_request[tid][mutex_id] = 0;
            return -0xDEAD;
        }

        // 安全，清除临时请求
        p->mutex_request[tid][mutex_id] = 0;
    }

    mutex_lock(&curr_proc()->mutex_pool[mutex_id]);
    return 0;
}

int sys_semaphore_down(int semaphore_id)
{
    if (semaphore_id < 0 || semaphore_id >= curr_proc()->next_semaphore_id)
        return -1;

    struct proc *p = curr_proc();
    int tid = curr_thread()->tid;

    if (p->deadlock_detect_enable) {
        p->sem_request[tid][semaphore_id] = 1;

        if (deadlock_detect(p)) {
            p->sem_request[tid][semaphore_id] = 0;
            return -0xDEAD;
        }

        p->sem_request[tid][semaphore_id] = 0;
    }

    semaphore_down(&curr_proc()->semaphore_pool[semaphore_id]);
    return 0;
}
```

### 3.8 启用/禁用死锁检测

```c
// os/syscall.c

int sys_enable_deadlock_detect(int is_enable)
{
    struct proc *p = curr_proc();
    p->deadlock_detect_enable = is_enable;
    return 0;
}
```

## 四、系统调用接口

| 系统调用 | 功能 |
|----------|------|
| `mutex_create(blocking)` | 创建互斥锁，返回 id |
| `mutex_lock(id)` | 获取锁，可能阻塞 |
| `mutex_unlock(id)` | 释放锁 |
| `semaphore_create(count)` | 创建信号量，初始值为 count |
| `semaphore_up(id)` | V 操作，释放资源 |
| `semaphore_down(id)` | P 操作，获取资源 |
| `condvar_create()` | 创建条件变量 |
| `condvar_signal(id)` | 唤醒一个等待者 |
| `condvar_wait(cond_id, mutex_id)` | 等待条件，自动释放/重获 mutex |
| `enable_deadlock_detect(enable)` | 启用/禁用死锁检测 |

**死锁检测返回值**：
- 正常：返回 0
- 检测到死锁：返回 `-0xDEAD` (-57005)

## 五、知识点总结

1. **互斥锁**：
   - 自旋锁：忙等待，适合短临界区
   - 阻塞锁：让出 CPU，适合长临界区

2. **信号量**：
   - count > 0：可用资源数
   - count <= 0：|count| 为等待线程数
   - P 操作：count--，可能阻塞
   - V 操作：count++，可能唤醒

3. **条件变量**：
   - 必须配合 mutex 使用
   - wait：原子性释放 mutex 并进入等待
   - signal：唤醒一个等待者

4. **银行家算法**：
   - 模拟资源分配，检测是否能安全完成
   - 时间复杂度 O(n²m)，n=线程数，m=资源类型数

5. **分配矩阵维护**：
   - 获取资源时：allocation++, request--
   - 释放资源时：allocation--
   - 等待资源时：request++

## 六、文件修改清单

| 文件 | 内容 |
|------|------|
| `os/sync.h` | 定义 mutex, semaphore, condvar 结构体 |
| `os/sync.c` | 实现同步原语的创建、获取、释放 |
| `os/proc.h` | 添加同步资源池和死锁检测矩阵 |
| `os/proc.c` | 初始化同步相关字段 |
| `os/syscall.c` | 系统调用封装，死锁检测算法 |

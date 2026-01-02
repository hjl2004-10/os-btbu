# ch8

## 第八章：并发与锁

## 1. sys_thread_create函数功能说明 (os/syscall.c:205)

**作用**：创建一个新的线程，该线程属于当前进程
### 函数定义

```c
int sys_thread_create(uint64 entry, uint64 arg)  // 函数名：系统调用-创建线程；参数entry：线程入口函数地址，arg：传递给线程函数的参数
{
	struct proc *p = curr_proc();  // 获取当前进程的指针，因为新线程属于当前进程
	int tid = allocthread(p, entry, 1);  // 调用allocthread分配线程资源：参数1表示需要分配用户栈，返回线程ID(tid)
	if (tid < 0) {  // 检查线程分配是否失败
		errorf("fail to create thread");  // 输出错误信息：创建线程失败
		return -1;  // 返回-1表示失败
	}
	struct thread *t = &p->threads[tid];  // 通过tid获取线程控制块的指针
	t->trapframe->a0 = arg;  // 将参数arg放入线程的trapframe的a0寄存器，这是RISC-V调用约定中第一个参数寄存器
	t->state = RUNNABLE;  // 将线程状态设置为RUNNABLE，表示线程可以被调度执行
	add_task(t);  // 将线程添加到任务队列，等待调度器调度
	return tid;  // 返回线程ID，用户程序可以通过这个ID等待或操作该线程
}
```

**工作原理**：
1. **进程关联**：新创建的线程属于当前进程（`curr_proc()`），共享进程的地址空间和资源
2. **资源分配**：通过 `allocthread()` 分配线程的内核栈、用户栈和陷阱帧
3. **参数传递**：将 `arg` 参数写入 `trapframe->a0`，线程启动后可以从 `a0` 寄存器读取参数
4. **状态设置**：线程状态设置为 `RUNNABLE`，表示已就绪可以被调度
5. **加入调度**：调用 `add_task()` 将线程加入任务队列，调度器会选择该线程执行

**线程与进程的关系**：
- 一个进程可以包含多个线程（最多 `NTHREAD` 个）
- 同一进程内的线程共享：地址空间、文件描述符、信号量、互斥锁等
- 每个线程有独立的：内核栈、用户栈、陷阱帧、线程ID（tid）

---

## 2. sys_waittid函数功能说明 (os/syscall.c:225)

**作用**：等待指定的线程结束，并回收线程资源
### 函数定义

```c
int sys_waittid(int tid)  // 函数名：系统调用-等待线程结束；参数tid：要等待的线程ID
{
	if (tid < 0 || tid >= NTHREAD) {  // 检查tid是否在有效范围[0, NTHREAD-1]内
		errorf("unexpected tid %d", tid);  // 输出错误信息：无效的线程ID
		return -1;  // 返回-1表示参数错误
	}
	struct thread *t = &curr_proc()->threads[tid];  // 获取当前进程中指定tid的线程控制块指针
	if (t->state == T_UNUSED || tid == curr_thread()->tid) {  // 检查线程是否未使用或等待自己
		return -1;  // 如果线程未使用或等待自己，返回-1（无效操作）
	}
	if (t->state != EXITED) {  // 检查线程是否已经退出
		return -2;  // 返回-2表示线程仍在运行，需要继续等待
	}
	memset((void *)t->kstack, 7, KSTACK_SIZE);  // 清空线程的内核栈，填充值7用于调试
	t->tid = -1;  // 将线程ID设置为-1，标记该线程槽位为可用
	t->state = T_UNUSED;  // 将线程状态设置为T_UNUSED，表示线程控制块可以被重用
	return t->exit_code;  // 返回线程的退出码，用户程序可以通过这个值获取线程的执行结果
}
```

**工作原理**：
1. **参数验证**：检查 `tid` 是否在合法范围内 `[0, NTHREAD)`
2. **状态检查**：
   - 如果线程未使用（`T_UNUSED`）或等待自己（`tid == curr_thread()->tid`），返回 `-1`
   - 如果线程仍在运行（非 `EXITED` 状态），返回 `-2`
   - 如果线程已退出（`EXITED` 状态），回收资源
3. **资源回收**：
   - 清空内核栈（用 `memset` 填充 `7`）
   - 重置线程ID为 `-1`
   - 标记线程状态为 `T_UNUSED`
4. **返回退出码**：返回线程的退出码，调用者可以通过这个值判断线程是否成功执行

**与用户态封装的配合**：
用户态的 `waittid()` 函数会循环调用 `sys_waittid()`，直到返回值不是 `-2`（即线程已退出）：
```c
int waittid(int tid)
{
    int ret = syscall(SYS_waittid, tid);  // 第一次调用
    while (ret == -2) {  // 如果线程仍在运行
        sched_yield();  // 让出CPU，调度其他线程
        ret = syscall(SYS_waittid, tid);  // 再次检查
    }
    return ret;  // 返回线程的退出码或错误码
}
```

**返回值说明**：
- `-1`：参数错误（无效的 tid）或逻辑错误（等待自己、线程不存在）
- `-2`：线程仍在运行，需要继续等待
- `>= 0`：线程已退出，返回值是线程的退出码

---

## 3. allocthread函数功能说明 (os/proc.c:159)

**作用**：为进程分配一个新的线程，包括内核栈、用户栈、陷阱帧等资源
### 函数定义

```c
int allocthread(struct proc *p, uint64 entry, int alloc_user_res)  // 函数名：分配线程；参数p：所属进程，entry：线程入口地址，alloc_user_res：是否分配用户资源(用户栈)
{
	int tid;
	struct thread *t;
	for (tid = 0; tid < NTHREAD; ++tid) {  // 遍历进程的线程槽位数组[0, NTHREAD-1]
		t = &p->threads[tid];  // 获取第tid个线程控制块的指针
		if (t->state == T_UNUSED) {  // 检查该槽位是否未被使用
			goto found;  // 找到可用槽位，跳转到found标签进行初始化
		}
	}
	return -1;  // 所有槽位都被占用，返回-1表示分配失败

found:  // 找到可用线程槽位，开始初始化
	t->tid = tid;  // 设置线程ID
	t->state = T_USED;  // 标记线程槽位为已使用
	t->process = p;  // 设置线程所属进程
	t->exit_code = 0;  // 初始化退出码为0

	// 内核栈分配
	t->kstack = (uint64)kstack[p - pool][tid];  // 从全局kstack数组中分配内核栈，kstack是二维数组[进程号][线程号]

	// 用户栈分配
	t->ustack = get_thread_ustack_base_va(t);  // 计算用户栈的虚拟地址基址，每个线程在用户地址空间有独立的栈区域
	if (alloc_user_res != 0) {  // 如果需要分配用户资源
		if (uvmmap(p->pagetable, t->ustack, USTACK_SIZE / PAGE_SIZE,
			   PTE_U | PTE_R | PTE_W) < 0) {  // 在页表中映射用户栈的物理内存，权限：用户可读|用户可写|可读
			panic("map ustack fail");  // 如果映射失败，系统崩溃
		}
		p->max_page =  // 更新进程使用的最大页号
			MAX(p->max_page,  // 取当前最大值和新值中的较大者
			    PGROUNDUP(t->ustack + USTACK_SIZE - 1) / PAGE_SIZE);  // 计算用户栈结束位置所在的页号
	}

	// 陷阱帧(Trap Frame)初始化
	t->trapframe = (struct trapframe *)trapframe[p - pool][tid];  // 从全局trapframe数组分配陷阱帧
	memset((void *)t->trapframe, 0, TRAP_PAGE_SIZE);  // 清空陷阱帧内存
	if (mappages(p->pagetable, get_thread_trapframe_va(tid), TRAP_PAGE_SIZE,
		     (uint64)t->trapframe, PTE_R | PTE_W) < 0) {  // 将陷阱帧映射到用户地址空间的TRAPFRAME区域
		panic("map trapframe fail");  // 映射失败则系统崩溃
	}
	t->trapframe->sp = t->ustack + USTACK_SIZE;  // 设置用户栈指针，指向栈顶（高地址）
	t->trapframe->epc = entry;  // 设置程序计数器，指向线程入口函数

	// 线程上下文初始化
	memset(&t->context, 0, sizeof(t->context));  // 清空内核态上下文结构
	t->context.ra = (uint64)usertrapret;  // 设置返回地址为usertrapret，线程首次被调度时会从内核返回用户态
	t->context.sp = t->kstack + KSTACK_SIZE;  // 设置内核栈指针，指向栈顶

	// 注意：这里不立即将线程加入调度器，由sys_thread_create负责
	debugf("allocthread p: %d, o: %d, t: %d, e: %p, sp: %p, spp: %p",
	       p->pid, (p - pool), t->tid, entry, t->ustack,
	       useraddr(p->pagetable, t->ustack));  // 输出调试信息
	return tid;  // 返回线程ID
}
```

**工作原理**：
1. **查找空闲槽位**：遍历进程的线程数组，找到状态为 `T_UNUSED` 的槽位
2. **初始化基本信息**：设置线程ID、所属进程、初始状态等
3. **内核栈分配**：从全局 `kstack[NPROC][NTHREAD]` 数组中分配内核栈
4. **用户栈分配**（可选）：
   - 如果 `alloc_user_res != 0`，则在页表中映射用户栈的物理页
   - 用户栈虚拟地址计算：`ustack_base + tid * USTACK_SIZE`
   - 每个线程在用户空间有独立的栈区域
5. **陷阱帧初始化**：
   - 分配并清零陷阱帧
   - 将陷阱帧映射到用户地址空间的固定位置（TRAPFRAME - tid * TRAP_PAGE_SIZE）
   - 设置用户栈指针（`sp`）和程序计数器（`epc`）
6. **内核态上下文初始化**：
   - 设置返回地址为 `usertrapret`，确保线程首次运行时能正确返回用户态
   - 设置内核栈指针

**内存布局**：
```
用户地址空间（每个进程独立）：
+------------------+ 0x00000000
|    代码段        |
+------------------+
|    数据段        |
+------------------+
|    堆            |  ↑ 向上增长
+------------------+
|                  |
|   线程0用户栈    |  ↓ 向下增长
|   (ustack+0)     |
+------------------+
|   线程1用户栈    |  ↓ 向下增长
|   (ustack+1*16KB)|
+------------------+
|   线程2用户栈    |
|   (ustack+2*16KB)|
+------------------+
|      ...         |
+------------------+
|  陷阱帧区域      |  TRAPFRAME - tid*4KB
+------------------+ TRAPFRAME (高地址)

内核地址空间（全局共享）：
kstack[NPROC][NTHREAD]  - 每个线程的内核栈
trapframe[NPROC][NTHREAD] - 每个线程的陷阱帧
```

**重要概念**：
- **内核栈 vs 用户栈**：线程在内核态执行时使用内核栈，在用户态执行时使用用户栈
- **陷阱帧**：保存用户态寄存器状态，在进入/退出内核时使用
- **上下文**：保存内核态寄存器状态，用于线程切换
- **线程入口**：通过设置 `trapframe->epc`，线程首次运行时会跳转到入口函数

---

## 4. exit函数功能说明 (os/proc.c:441)

**作用**：退出当前线程，如果是主线程(tid=0)则退出整个进程
### 函数定义

```c
void exit(int code)  // 函数名：线程/进程退出；参数code：退出码，表示线程或进程的退出状态
{
	struct proc *p = curr_proc();  // 获取当前进程的指针
	struct thread *t = curr_thread();  // 获取当前线程的指针
	t->exit_code = code;  // 将退出码保存到线程控制块，供waittid()读取
	t->state = EXITED;  // 将线程状态设置为EXITED，表示线程已退出
	int tid = t->tid;  // 保存当前线程的ID
	debugf("thread exit with %d", code);  // 输出调试信息
	freethread(t);  // 释放线程占用的部分资源（如用户栈）

	if (tid == 0) {  // 如果是主线程（tid==0），则需要退出整个进程
		p->exit_code = code;  // 保存进程退出码
		freeproc(p);  // 释放进程占用的资源（页表、打开的文件等）
		debugf("proc exit");  // 输出调试信息
		if (p->parent != NULL) {  // 如果进程有父进程
			// Parent should `wait`  // 父进程应该调用wait()等待
			p->state = ZOMBIE;  // 将进程状态设置为ZOMBIE（僵尸状态），等待父进程回收
		}
		// Set the `parent` of all children to NULL  // 将所有子进程的父指针设置为NULL
		struct proc *np;
		for (np = pool; np < &pool[NPROC]; np++) {  // 遍历进程池
			if (np->parent == p) {  // 找到当前进程的子进程
				np->parent = NULL;  // 将子进程的父指针设置为NULL，成为孤儿进程
			}
		}
	}
	sched();  // 调用调度器，切换到其他线程执行，当前线程不会再被调度
}
```

**工作原理**：
1. **线程退出**：
   - 保存退出码到 `t->exit_code`
   - 设置线程状态为 `EXITED`
   - 调用 `freethread()` 释放线程资源（用户栈页表映射）
   - 触发调度，线程不再执行

2. **进程退出**（仅主线程 tid==0）：
   - 保存进程退出码到 `p->exit_code`
   - 调用 `freeproc()` 释放进程资源：
     - 页表
     - 打开的文件描述符
     - 信号量、互斥锁等同步机制
   - 如果有父进程，将进程状态设为 `ZOMBIE`（僵尸进程）
   - 将所有子进程的父指针设为 `NULL`（成为孤儿进程）

3. **调度切换**：
   - 调用 `sched()` 触发线程切换
   - 当前线程/进程不会再被调度执行

**线程 vs 进程退出**：
- **线程退出**（tid != 0）：
  - 只退出当前线程
  - 其他线程继续执行
  - 线程资源被标记为可回收
  - 进程继续存在

- **进程退出**（tid == 0，主线程）：
  - 整个进程退出
  - 所有线程都被终止
  - 进程资源被释放或标记为可回收
  - 进程变为僵尸状态（ZOMBIE），等待父进程回收

**与 threads 程序的对应关系**：
```c
void thread_a() {
    for (int i = 0; i < LOOP; ++i) {
        putchar('a');
    }
    exit(1);  // ← 线程a退出，退出码为1，主线程通过waittid()获取这个值
}

void thread_b() {
    for (int i = 0; i < LOOP; ++i) {
        putchar('b');
    }
    exit(2);  // ← 线程b退出，退出码为2
}

void thread_c() {
    for (int i = 0; i < LOOP; ++i) {
        putchar('c');
    }
    exit(3);  // ← 线程c退出，退出码为3
}

int main(void) {
    int tids[NTHREAD];
    tids[0] = thread_create(thread_a, 0);  // 创建线程a，tid可能是0、1或2
    tids[1] = thread_create(thread_b, 0);  // 创建线程b
    tids[2] = thread_create(thread_c, 0);  // 创建线程c
    for (int i = 0; i < NTHREAD; ++i) {
        int tid = tids[i];
        int exit_code = waittid(tid);  // 等待线程退出，获取退出码
        printf("thread %d exited with code %d\n", tid, exit_code);
        assert_eq(tid, exit_code);  // 验证退出码是否等于tid
    }
    puts("threads test passed!");
    return 0;  // 主线程退出，整个进程结束
}
```

**状态转换图**：
```
线程状态：
T_UNUSED → T_USED → RUNNABLE → RUNNING → EXITED → (等待sys_waittid回收) → T_UNUSED

进程状态：
P_UNUSED → P_USED → RUNNABLE → RUNNING → ZOMBIE → (等待父进程wait回收) → P_UNUSED
```

---

## 5. thread_create函数功能说明 (用户态库函数)

**作用**：用户态线程创建封装函数，在首次创建线程时启用线程IO缓冲，然后调用系统调用创建线程
### 函数定义

```c
int thread_create(void *entry, void *arg)  // 函数名：用户态线程创建；参数entry：线程入口函数指针，arg：传递给线程的参数
{
	// on first thread create enable, here must be single thread  // 第一次创建线程时启用缓冲，此时必须是单线程环境
	if (!buffer_lock_enabled) {  // 检查IO缓冲锁是否已启用
		enable_thread_io_buffer();  // 启用线程安全的IO缓冲机制，防止多线程并发输出时产生乱序
	}
	return syscall(SYS_thread_create, (uint64)entry, (uint64)arg);  // 调用系统调用SYS_thread_create，将指针转换为uint64传递给内核
}
```

**工作原理**：
1. **IO缓冲初始化**：
   - 检查 `buffer_lock_enabled` 标志，判断是否已经启用线程安全的IO缓冲
   - 如果未启用，调用 `enable_thread_io_buffer()` 初始化
   - 这确保在单线程环境下完成初始化，避免竞态条件

2. **系统调用**：
   - 使用 `syscall()` 封装函数调用内核的 `sys_thread_create`
   - 传递线程入口函数地址和参数
   - 返回值是新创建线程的ID（tid）

**为什么需要IO缓冲？**
在多线程环境下，如果多个线程同时调用 `printf()` 或 `putchar()`，可能出现输出交错的情况：
```
期望输出：aaaaabbbbbccccc
实际输出：aabacbabcbcacbc  ← 字符混乱
```

启用IO缓冲后，每个线程的输出会被缓冲，在适当时机统一刷新，避免字符混乱。

**调用流程**：
```
用户程序：thread_create(thread_a, 0)
    ↓
用户库：检查IO缓冲 → syscall(SYS_thread_create, entry, arg)
    ↓
内核：sys_thread_create() → allocthread() → add_task()
    ↓
返回：线程ID (tid)
```

---

## 6. waittid函数功能说明 (用户态库函数)

**作用**：用户态线程等待封装函数，循环等待指定线程退出，并获取其退出码
### 函数定义

```c
int waittid(int tid)  // 函数名：用户态线程等待；参数tid：要等待的线程ID
{
	int ret = syscall(SYS_waittid, tid);  // 第一次调用系统调用SYS_waittid，尝试获取线程退出状态
	while (ret == -2) {  // 如果返回-2，表示线程仍在运行，需要继续等待
		sched_yield();  // 让出CPU，允许其他线程运行，避免忙等待浪费CPU资源
		ret = syscall(SYS_waittid, tid);  // 再次调用SYS_waittid检查线程状态
	}
	return ret;  // 返回线程的退出码（>=0）或错误码（-1）
}
```

**工作原理**：
1. **首次检查**：
   - 调用 `syscall(SYS_waittid, tid)` 尝试获取线程状态
   - 可能的返回值：
     - `-1`：参数错误（无效的tid）或逻辑错误（等待自己、线程不存在）
     - `-2`：线程仍在运行，需要继续等待
     - `>= 0`：线程已退出，返回值是退出码

2. **循环等待**：
   - 如果返回 `-2`（线程仍在运行），进入循环
   - 调用 `sched_yield()` 主动让出CPU，触发线程调度
   - 再次调用 `syscall(SYS_waittid, tid)` 检查线程状态

3. **退出条件**：
   - 当返回值不是 `-2` 时退出循环
   - 返回线程的退出码或错误码

**为什么需要循环等待？**
内核的 `sys_waittid()` 采用**非阻塞设计**：
- **优点**：内核实现简单，不需要维护等待队列和唤醒机制
- **缺点**：需要用户态循环轮询

用户态封装通过循环+`sched_yield()`实现**忙等待的优化版本**：
```
传统忙等待（浪费CPU）：
while (thread_not_exited) {
    // 空转，CPU 100%占用
}

优化后（协作式等待）：
while (ret == -2) {
    sched_yield();  // 让出CPU，允许其他线程运行
    ret = syscall(SYS_waittid, tid);
}
```

**与内核sys_waittid的配合**：
```
用户态：waittid(1)
    ↓
循环：syscall(SYS_waittid, 1) → 返回-2（线程仍在运行）
    ↓
    sched_yield() → 切换到线程1运行
    ↓
    线程1执行... → exit(100)
    ↓
循环：syscall(SYS_waittid, 1) → 返回100（线程已退出）
    ↓
返回：100
```

**完整示例**：
```c
// 用户程序示例
void thread_func() {
    printf("Thread running\n");
    for (int i = 0; i < 1000000; i++) {
        // 执行任务
    }
    exit(42);  // 退出码为42
}

int main() {
    int tid = thread_create(thread_func, NULL);  // 创建线程
    printf("Thread %d created\n", tid);

    int exit_code = waittid(tid);  // 等待线程退出
    printf("Thread %d exited with code %d\n", tid, exit_code);  // 输出: Thread 1 exited with code 42

    return 0;
}
```

**返回值说明**：
- `-1`：参数错误或逻辑错误
  - 无效的tid（超出范围）
  - 线程不存在（T_UNUSED）
  - 尝试等待自己（tid == curr_thread()->tid）
- `>= 0`：线程的退出码
  - 由线程调用 `exit(code)` 时设置
  - 例如：`exit(0)` → 返回0，`exit(42)` → 返回42

---

## 7. 多线程测试程序 (user/src/ch8b_threads.c)

**作用**：演示多线程的创建、并发执行和等待机制，测试线程系统的正确性

### 完整代码

```c
//user/src/ch8b_threads.c
...
#define LOOP 1000  // 每个线程循环打印的次数
#define NTHREAD 3  // 创建的线程数量

void thread_a()  // 线程a的入口函数
{
	for (int i = 0; i < LOOP; ++i) {  // 循环1000次
		putchar('a');  // 每次打印字符'a'
	}
	exit(1);  // 退出线程，退出码为1（与tid对应）
}

void thread_b()  // 线程b的入口函数
{
	for (int i = 0; i < LOOP; ++i) {  // 循环1000次
		putchar('b');  // 每次打印字符'b'
	}
	exit(2);  // 退出线程，退出码为2
}

void thread_c()  // 线程c的入口函数
{
	for (int i = 0; i < LOOP; ++i) {  // 循环1000次
		putchar('c');  // 每次打印字符'c'
	}
	exit(3);  // 退出线程，退出码为3
}

int main(void)  // 主线程（tid=0）
{
	int tids[NTHREAD];  // 存储3个线程的ID
	tids[0] = thread_create(thread_a, 0);  // 创建线程a，第二个参数0表示不传递参数给线程
	tids[1] = thread_create(thread_b, 0);  // 创建线程b
	tids[2] = thread_create(thread_c, 0);  // 创建线程c
	for (int i = 0; i < NTHREAD; ++i) {  // 循环等待所有线程退出
		int tid = tids[i];  // 获取第i个线程的ID
		int exit_code = waittid(tid);  // 等待线程tid退出，并获取退出码
		printf("thread %d exited with code %d\n", tid, exit_code);  // 打印线程退出信息
		assert_eq(tid, exit_code);  // 断言：退出码应该等于tid（验证exit()参数传递正确）
	}
	puts("threads test passed!");  // 所有测试通过，打印成功信息
	return 0;  // 主线程退出，整个进程结束
}
```

### 程序执行流程

#### 1. 主线程创建3个子线程
```
时刻T0：主线程(tid=0)启动
    ↓
时刻T1：thread_create(thread_a, 0) → 创建线程a (假设tid=1)
    ↓
时刻T2：thread_create(thread_b, 0) → 创建线程b (假设tid=2)
    ↓
时刻T3：thread_create(thread_c, 0) → 创建线程c (假设tid=3)
```

#### 2. 四个线程并发执行
此时系统中有4个线程：
- **主线程(tid=0)**：进入循环，等待子线程退出
- **线程a(tid=1)**：打印'a' × 1000次
- **线程b(tid=2)**：打印'b' × 1000次
- **线程c(tid=3)**：打印'c' × 1000次

调度器会在这4个线程之间切换，可能的输出：
```
aaabbbcccaaabbbcccaaabbb...  ← 线程交替执行
```

#### 3. 主线程等待子线程退出
```c
// 第一次循环 (i=0)
waittid(tids[0]);  // 等待线程a退出
    ↓
    如果线程a仍在运行 → sched_yield() → 切换到其他线程
    ↓
    线程a执行完毕，调用exit(1)
    ↓
    waittid()返回1（退出码）
    ↓
printf("thread 1 exited with code 1\n");
assert_eq(1, 1);  // 通过

// 第二次循环 (i=1)
waittid(tids[1]);  // 等待线程b退出
    ↓
    ... (类似流程)
    ↓
printf("thread 2 exited with code 2\n");
assert_eq(2, 2);  // 通过

// 第三次循环 (i=2)
waittid(tids[2]);  // 等待线程c退出
    ↓
    ... (类似流程)
    ↓
printf("thread 3 exited with code 3\n");
assert_eq(3, 3);  // 通过
```

#### 4. 测试通过，主线程退出
```c
puts("threads test passed!");
return 0;  // 主线程退出 → 调用exit(0) → 进程结束
```

### 线程生命周期时间线

```
时间轴：
T0: [主线程] 启动
T1: [主线程] 创建线程a → [线程a] RUNNABLE
T2: [主线程] 创建线程b → [线程b] RUNNABLE
T3: [主线程] 创建线程c → [线程c] RUNNABLE
T4: [主线程] waittid(1)，进入等待循环
    [线程a] 开始打印'a'
    [线程b] 开始打印'b'
    [线程c] 开始打印'c'
    ... (多次线程切换) ...
T5: [线程a] 打印完1000个'a'，调用exit(1) → EXITED
T6: [主线程] waittid(1)返回1，打印"thread 1 exited with code 1"
T7: [主线程] waittid(2)，进入等待循环
    [线程b] 继续打印'b'
    [线程c] 继续打印'c'
T8: [线程b] 打印完1000个'b'，调用exit(2) → EXITED
T9: [主线程] waittid(2)返回2，打印"thread 2 exited with code 2"
T10:[主线程] waittid(3)，进入等待循环
     [线程c] 继续打印'c'
T11:[线程c] 打印完1000个'c'，调用exit(3) → EXITED
T12:[主线程] waittid(3)返回3，打印"thread 3 exited with code 3"
T13:[主线程] 打印"threads test passed!"
T14:[主线程] return 0 → 进程退出
```

### 预期输出示例

```
aabcabcabcbabcabcabc...bcabcabc  ← 3000个字符(a/b/c混合)
thread 1 exited with code 1
thread 2 exited with code 2
thread 3 exited with code 3
threads test passed!
```

**注意**：
- 前3000个字符的顺序是**不确定的**，取决于调度器的调度策略
- 但每个字符('a'/'b'/'c')各有1000个
- 线程退出的顺序也是**不确定的**，取决于执行速度
- 退出码的验证(`assert_eq(tid, exit_code)`)确保了线程退出机制的正确性

### 测试的关键点

#### 1. 线程创建
- 验证 `thread_create()` 能正确创建线程
- 验证线程能获得独立的执行流
- 验证线程ID分配的正确性

#### 2. 并发执行
- 验证多个线程能同时运行
- 验证调度器的线程切换机制
- 验证每个线程有独立的栈空间（否则会相互覆盖）

#### 3. 线程等待
- 验证 `waittid()` 能正确等待线程退出
- 验证退出码传递机制（`exit(code)` → `waittid()` 返回值）
- 验证主线程能等待所有子线程完成

#### 4. 线程退出
- 验证 `exit()` 能正确终止线程
- 验证线程资源能被正确回收
- 验证主线程退出时整个进程结束

### IO缓冲的作用

**没有IO缓冲时**（可能的输出）：
```
aabacbabcbcacbc...  ← 字符混乱，输出交错
```
每个 `putchar('a')` 直接写入控制台，线程切换时可能打印到一半。

**有IO缓冲时**（优化后的输出）：
```
aaaaaa...bbbbbb...cccccc...  ← 字符成块输出
```
每个线程的输出被缓冲，在适当时机（如缓冲区满、线程退出）统一刷新。

### 与前面函数的对应关系

| 用户程序代码 | 调用的函数 | 内核处理 |
|-------------|-----------|----------|
| `thread_create(thread_a, 0)` | 5. `thread_create` → syscall | 1. `sys_thread_create` → 3. `allocthread` |
| `exit(1)` | 直接syscall | 4. `exit` |
| `waittid(tid)` | 6. `waittid` → syscall循环 | 2. `sys_waittid` |

### 可能的扩展测试

1. **传递参数给线程**：
```c
void thread_with_arg(void *arg) {
    int n = (int)(uint64)arg;
    printf("Thread received arg: %d\n", n);
    exit(n);
}

int main() {
    int tid = thread_create(thread_with_arg, (void *)42);
    int code = waittid(tid);
    printf("Exit code: %d\n", code);  // 输出: Exit code: 42
}
```

2. **线程间共享数据**（需要同步机制）：
```c
int counter = 0;  // 全局变量，所有线程共享

void increment() {
    for (int i = 0; i < 1000; i++) {
        counter++;  // ⚠️ 竞态条件！需要锁保护
    }
    exit(0);
}
```

3. **测试线程数量上限**：
```c
int main() {
    int tids[NTHREAD];  // NTHREAD是系统支持的最大线程数
    for (int i = 0; i < NTHREAD; i++) {
        tids[i] = thread_create(thread_func, 0);
        if (tids[i] < 0) {
            printf("Failed to create thread %d\n", i);
        }
    }
}
```

---

## 8. 互斥锁用户态封装函数 (usr/lib/syscall.c)

**作用**：提供互斥锁的用户态接口，包括创建、加锁、解锁操作

### 函数定义

```c
// usr/lib/syscall.c
int mutex_create()  // 函数名：创建非阻塞互斥锁
{
	return syscall(SYS_mutex_create, 0);  // 调用系统调用SYS_mutex_create，参数0表示非阻塞模式（自旋锁模式）
}

int mutex_blocking_create()  // 函数名：创建阻塞互斥锁
{
	return syscall(SYS_mutex_create, 1);  // 调用系统调用SYS_mutex_create，参数1表示阻塞模式（睡眠等待）
}

int mutex_lock(int mid)  // 函数名：获取互斥锁；参数mid：互斥锁ID
{
	return syscall(SYS_mutex_lock, mid);  // 调用系统调用SYS_mutex_lock，尝试获取互斥锁
}

int mutex_unlock(int mid)  // 函数名：释放互斥锁；参数mid：互斥锁ID
{
	return syscall(SYS_mutex_unlock, mid);  // 调用系统调用SYS_mutex_unlock，释放持有的互斥锁
}
```

### 互斥锁类型说明

#### 1. 非阻塞互斥锁（Spinlock模式）
```c
int mid = mutex_create();  // 参数0，自旋锁模式
```
- **行为**：如果锁已被占用，调用 `mutex_lock()` 的线程会**忙等待**（自旋）
- **优点**：响应快，适合锁持有时间短的场景
- **缺点**：浪费CPU资源，不适合长时间等待
- **应用场景**：临界区代码执行时间非常短（微秒级）

#### 2. 阻塞互斥锁（Blocking模式）
```c
int mid = mutex_blocking_create();  // 参数1，阻塞模式
```
- **行为**：如果锁已被占用，调用 `mutex_lock()` 的线程会**让出CPU**，进入等待队列
- **优点**：节省CPU资源，适合锁持有时间长的场景
- **缺点**：线程切换有开销
- **应用场景**：临界区代码执行时间较长（毫秒级及以上）

### 互斥锁操作流程

```
创建锁：
mutex_blocking_create() → syscall(SYS_mutex_create, 1) → 内核分配锁资源 → 返回锁ID(mid)

获取锁：
mutex_lock(mid) → syscall(SYS_mutex_lock, mid) → 内核检查锁状态
    ↓
    锁可用 → 设置锁为占用 → 返回0（成功）
    ↓
    锁被占用（阻塞模式） → 线程进入等待队列 → 让出CPU → 被唤醒后重试 → 返回0
    ↓
    锁被占用（非阻塞模式） → 自旋等待（循环重试） → 返回0

释放锁：
mutex_unlock(mid) → syscall(SYS_mutex_unlock, mid) → 内核释放锁 → 唤醒等待队列中的线程 → 返回0
```

### 返回值说明

- **mutex_create / mutex_blocking_create**：
  - `>= 0`：互斥锁ID（成功）
  - `< 0`：创建失败（锁资源耗尽）

- **mutex_lock**：
  - `0`：成功获取锁
  - `< 0`：参数错误（无效的mid）

- **mutex_unlock**：
  - `0`：成功释放锁
  - `< 0`：参数错误或锁未被当前线程持有

---

## 9. 互斥锁竞态测试程序 (user/src/ch8b_mut_race.c)

**作用**：测试互斥锁能否正确防止多线程竞态条件，验证临界区的互斥访问

### 完整代码

```c
// user/src/ch8b_mut_race.c
...
int mutex_id;  // 全局互斥锁ID
int a;  // 共享变量，所有线程都会修改它
int threads[thread_count];  // 存储线程ID数组

void fun(long i)  // 线程入口函数；参数i：线程编号
{
	int t = i + 1;  // 局部变量，用于计算（避免编译器优化掉循环）
	for (int i = 0; i < per_thread; i++) {  // 每个线程循环per_thread次
		assert_eq(mutex_lock(mutex_id), 0);  // 🔒 获取互斥锁，进入临界区
		int old_a = a;  // 读取共享变量a的当前值
		for (int i = 0; i < 500; i++) {  // 执行耗时计算，模拟临界区操作
			t = t * t % 10007;  // 计算操作（延长临界区时间）
		}
		a = old_a + 1;  // 将a的值加1（关键操作：读-改-写）
		assert_eq(mutex_unlock(mutex_id), 0);  // 🔓 释放互斥锁，离开临界区
	}
	exit(t);  // 线程退出，返回计算结果
}

int main()
{
	int64 start = get_mtime();  // 记录开始时间
	assert((mutex_id = mutex_blocking_create()) >= 0);  // 创建阻塞互斥锁，确保创建成功
	for (int i = 0; i < thread_count; i++) {  // 创建thread_count个线程
		threads[i] = thread_create(fun, (void *)i);  // 创建线程，传递线程编号i作为参数
		assert(threads[i] > 0);  // 确保线程创建成功
	}
	...  // 等待所有线程完成，验证结果
}
```

### 程序测试的关键问题

#### 1. 竞态条件（Race Condition）

**什么是竞态条件？**
多个线程同时访问共享变量，执行结果取决于线程执行顺序的情况。

**没有互斥锁时的问题**：
```c
// 线程A执行：
int old_a = a;  // 读取a=0
a = old_a + 1;  // 写入a=1

// 同时，线程B执行：
int old_a = a;  // 读取a=0（线程A还没写入）
a = old_a + 1;  // 写入a=1

// 结果：a=1（错误！应该是2）
```

**时间线示例（无锁）**：
```
时刻T0: a = 0
时刻T1: [线程A] 读取a → old_a = 0
时刻T2: [线程B] 读取a → old_a = 0  ← 竞态条件！
时刻T3: [线程A] 计算...
时刻T4: [线程B] 计算...
时刻T5: [线程A] 写入a = 1
时刻T6: [线程B] 写入a = 1  ← 覆盖了线程A的结果！
最终: a = 1（错误，期望值应为2）
```

#### 2. 互斥锁如何解决竞态条件

**有互斥锁时的执行**：
```c
// 线程A执行：
mutex_lock(mid);      // 获取锁
int old_a = a;        // 读取a=0
a = old_a + 1;        // 写入a=1
mutex_unlock(mid);    // 释放锁

// 线程B执行：
mutex_lock(mid);      // 尝试获取锁，但锁被线程A持有 → 等待
                      // （等待线程A释放锁）
mutex_lock(mid);      // 线程A释放锁后，线程B获取锁
int old_a = a;        // 读取a=1（正确值）
a = old_a + 1;        // 写入a=2
mutex_unlock(mid);    // 释放锁
```

**时间线示例（有锁）**：
```
时刻T0: a = 0
时刻T1: [线程A] mutex_lock() → 获取锁
时刻T2: [线程B] mutex_lock() → 锁被占用，进入等待队列，让出CPU
时刻T3: [线程A] 读取a → old_a = 0
时刻T4: [线程A] 计算...
时刻T5: [线程A] 写入a = 1
时刻T6: [线程A] mutex_unlock() → 释放锁，唤醒线程B
时刻T7: [线程B] 被唤醒，mutex_lock() → 获取锁
时刻T8: [线程B] 读取a → old_a = 1  ← 正确值！
时刻T9: [线程B] 计算...
时刻T10:[线程B] 写入a = 2
时刻T11:[线程B] mutex_unlock() → 释放锁
最终: a = 2（正确！）
```

### 临界区（Critical Section）

**定义**：访问共享资源的代码段，同一时刻只能有一个线程执行。

在本程序中，临界区是：
```c
mutex_lock(mutex_id);       // ← 临界区入口
// --- 临界区开始 ---
int old_a = a;              // 读取共享变量
for (int i = 0; i < 500; i++) {
    t = t * t % 10007;      // 耗时计算
}
a = old_a + 1;              // 修改共享变量
// --- 临界区结束 ---
mutex_unlock(mutex_id);     // ← 临界区出口
```

**临界区的性质**：
1. **互斥性**：同一时刻只有一个线程在临界区内
2. **有限等待**：等待进入临界区的时间是有限的
3. **空闲让进**：如果临界区空闲，应该允许线程立即进入
4. **有限执行**：临界区代码应该尽快执行完毕

### 预期测试结果

假设 `thread_count = 3`，`per_thread = 1000`：

**正确结果**（有互斥锁）：
```
初始值: a = 0
线程1: 执行1000次 a++
线程2: 执行1000次 a++
线程3: 执行1000次 a++
最终值: a = 3000  ✅ 正确！
```

**错误结果**（无互斥锁，有竞态条件）：
```
初始值: a = 0
线程1、2、3: 并发执行，多次发生竞态条件
最终值: a = 2847  ❌ 错误！（小于3000）
```

### 测试验证

程序通过以下方式验证互斥锁的正确性：
```c
// main函数的验证部分（省略的代码）
for (int i = 0; i < thread_count; i++) {
    waittid(threads[i]);  // 等待所有线程完成
}
assert_eq(a, thread_count * per_thread);  // 验证a的最终值是否正确
printf("mutex test passed! a = %d\n", a);  // 输出: mutex test passed! a = 3000
```

### 为什么临界区有耗时计算？

```c
for (int i = 0; i < 500; i++) {
    t = t * t % 10007;  // 耗时计算
}
```

**目的**：
1. **延长临界区时间**：增加竞态条件发生的概率
2. **防止编译器优化**：确保 `old_a` 和 `a` 之间有时间间隔
3. **模拟真实场景**：实际应用中临界区通常有复杂操作

如果没有这个延迟，竞态条件可能不容易被触发：
```c
// 没有延迟，执行太快
int old_a = a;
a = old_a + 1;  // 几纳秒就完成，竞态条件难以触发
```

### 互斥锁性能影响

```c
int64 start = get_mtime();  // 记录开始时间
// ... 执行测试 ...
int64 end = get_mtime();
printf("Time with mutex: %ld ms\n", end - start);
```

**预期结果**：
- **串行执行**：由于互斥锁强制临界区串行执行，3个线程的执行时间 ≈ 单线程时间 × 3
- **开销分析**：
  - 加锁/解锁的系统调用开销
  - 线程切换的上下文切换开销
  - 等待队列的管理开销

### 与前面函数的对应关系

| 用户程序代码 | 用户态库函数 | 内核系统调用 |
|-------------|------------|-------------|
| `mutex_blocking_create()` | 8. `mutex_blocking_create` → syscall | `sys_mutex_create(1)` |
| `mutex_lock(mid)` | 8. `mutex_lock` → syscall | `sys_mutex_lock(mid)` |
| `mutex_unlock(mid)` | 8. `mutex_unlock` → syscall | `sys_mutex_unlock(mid)` |
| `thread_create(fun, i)` | 5. `thread_create` → syscall | 1. `sys_thread_create` |
| `exit(t)` | 直接syscall | 4. `exit` |

---

## 10. 进程控制块与互斥锁数据结构 (os/proc.h, os/sync.h)

**作用**：定义进程和互斥锁的核心数据结构，管理线程资源和同步机制

### 数据结构定义

#### 进程控制块中的互斥锁池

```c
struct proc {  // 进程控制块
  ...
  uint next_mutex_id;  // 下一个可分配的互斥锁ID，用于唯一标识互斥锁
  struct mutex mutex_pool[LOCK_POOL_SIZE];  // 互斥锁池数组，每个进程最多创建LOCK_POOL_SIZE个互斥锁
};
```

**字段说明**：
- **next_mutex_id**：互斥锁ID计数器，从0开始递增
  - 每次创建新互斥锁时，使用 `next_mutex_id` 作为新锁的ID
  - 创建后 `next_mutex_id++`，指向下一个可用ID
  - 当互斥锁被删除时，ID不会被回收（简化设计）
  - 范围：`[0, LOCK_POOL_SIZE-1]`

- **mutex_pool[LOCK_POOL_SIZE]**：互斥锁池，固定大小的数组
  - 每个进程拥有独立的互斥锁池
  - 互斥锁ID直接作为数组索引：`mutex_pool[mid]`
  - 锁的数量限制：`LOCK_POOL_SIZE`（通常是16或32）

#### 互斥锁结构体

```c
struct mutex {  // 互斥锁结构体
  uint blocking;  // 阻塞模式标志：0=非阻塞（自旋锁），1=阻塞（睡眠等待）
  uint locked;  // 锁状态标志：0=未锁定，1=已锁定
  struct queue wait_queue;  // 等待队列，存储等待该锁的线程
  // "alloc" data for wait queue
  int _wait_queue_data[WAIT_QUEUE_MAX_LENGTH];  // 等待队列的静态分配数据存储
};
```

**字段说明**：

##### 1. **blocking** - 阻塞模式标志
```c
uint blocking;  // 0 或 1
```
- **blocking = 0**：非阻塞模式（自旋锁 Spinlock）
  - 线程尝试获取锁时，如果锁已被占用，会**忙等待**（自旋）
  - 优点：响应快，无线程切换开销
  - 缺点：浪费CPU资源
  - 适用场景：临界区执行时间极短（微秒级）

- **blocking = 1**：阻塞模式（Blocking）
  - 线程尝试获取锁时，如果锁已被占用，会**让出CPU**，进入等待队列
  - 优点：节省CPU资源
  - 缺点：有线程切换开销
  - 适用场景：临界区执行时间较长（毫秒级及以上）

##### 2. **locked** - 锁状态标志
```c
uint locked;  // 0 或 1
```
- **locked = 0**：锁未被占用（空闲状态）
  - 任何线程可以获取该锁
  - 等待队列为空

- **locked = 1**：锁已被占用
  - 某个线程持有该锁
  - 其他尝试获取锁的线程会：
    - 非阻塞模式：自旋等待（循环重试）
    - 阻塞模式：进入 `wait_queue` 等待

##### 3. **wait_queue** - 等待队列
```c
struct queue wait_queue;  // 等待队列数据结构
```
- **作用**：存储所有等待该锁的线程
- **数据结构**：队列（FIFO，先进先出）
- **操作**：
  - 入队：线程无法获取锁时，加入队尾
  - 出队：锁被释放时，从队首唤醒一个线程
- **阻塞模式专用**：非阻塞模式不使用等待队列

##### 4. **_wait_queue_data** - 等待队列存储
```c
int _wait_queue_data[WAIT_QUEUE_MAX_LENGTH];  // 静态分配的数组
```
- **作用**：为 `wait_queue` 提供静态存储空间
- **大小**：`WAIT_QUEUE_MAX_LENGTH`（最大等待线程数）
- **设计原因**：避免动态内存分配，简化内核实现
- **命名约定**：前缀 `_` 表示内部使用，用户不应直接访问

### 内存布局

```
进程地址空间：
+----------------------------------+
| struct proc                      |
+----------------------------------+
| pid                              |  进程ID
| state                            |  进程状态
| parent                           |  父进程指针
| ...                              |
| next_mutex_id: 3                 |  下一个互斥锁ID（当前已分配0,1,2）
+----------------------------------+
| mutex_pool[LOCK_POOL_SIZE]       |  互斥锁池
+----------------------------------+
| [0] mutex                        |
|     blocking: 1                  |  阻塞模式
|     locked: 0                    |  未锁定
|     wait_queue: empty            |  等待队列为空
|     _wait_queue_data[...]        |  等待队列存储
+----------------------------------+
| [1] mutex                        |
|     blocking: 1                  |  阻塞模式
|     locked: 1                    |  已锁定（被某线程持有）
|     wait_queue: [tid2, tid5]     |  等待队列有2个线程
|     _wait_queue_data[...]        |  等待队列存储
+----------------------------------+
| [2] mutex                        |
|     blocking: 0                  |  非阻塞模式（自旋锁）
|     locked: 1                    |  已锁定
|     wait_queue: unused           |  不使用等待队列
|     _wait_queue_data[...]        |  未使用
+----------------------------------+
| [3] ...                          |  未分配
| ...                              |
| [LOCK_POOL_SIZE-1] ...           |
+----------------------------------+
| threads[NTHREAD]                 |  线程池
| ...                              |
+----------------------------------+
```

### 互斥锁生命周期

#### 1. 创建互斥锁
```c
// 用户态：int mid = mutex_blocking_create();
// 内核态：sys_mutex_create(1)

int mid = p->next_mutex_id;  // 获取当前可用的ID，例如mid=0
struct mutex *m = &p->mutex_pool[mid];  // 通过ID获取互斥锁指针
m->blocking = 1;  // 设置为阻塞模式
m->locked = 0;  // 初始状态为未锁定
queue_init(&m->wait_queue);  // 初始化等待队列
p->next_mutex_id++;  // 递增ID计数器，指向下一个可用ID（变为1）
return mid;  // 返回互斥锁ID给用户程序
```

#### 2. 获取互斥锁（阻塞模式）
```c
// 用户态：mutex_lock(mid);
// 内核态：sys_mutex_lock(mid)

struct mutex *m = &p->mutex_pool[mid];  // 通过ID获取互斥锁
if (m->locked == 0) {  // 如果锁未被占用
    m->locked = 1;  // 设置为已锁定
    return 0;  // 成功获取锁
} else {  // 锁已被占用
    if (m->blocking == 1) {  // 阻塞模式
        queue_push(&m->wait_queue, curr_thread());  // 当前线程加入等待队列
        sched();  // 让出CPU，切换到其他线程
        // 被唤醒后，重新尝试获取锁
    } else {  // 非阻塞模式（自旋锁）
        while (m->locked == 1) {  // 自旋等待
            // 空转，等待锁释放
        }
        m->locked = 1;
    }
    return 0;
}
```

#### 3. 释放互斥锁（阻塞模式）
```c
// 用户态：mutex_unlock(mid);
// 内核态：sys_mutex_unlock(mid)

struct mutex *m = &p->mutex_pool[mid];  // 通过ID获取互斥锁
m->locked = 0;  // 设置锁为未占用
if (m->blocking == 1 && !queue_empty(&m->wait_queue)) {  // 如果是阻塞模式且等待队列非空
    struct thread *t = queue_pop(&m->wait_queue);  // 从队首取出一个等待的线程
    t->state = RUNNABLE;  // 设置线程状态为就绪
    add_task(t);  // 将线程加入调度队列
}
return 0;  // 成功释放锁
```

### 两种互斥锁模式对比

| 特性 | 非阻塞模式（blocking=0） | 阻塞模式（blocking=1） |
|------|----------------------|-------------------|
| **获取锁失败时的行为** | 自旋等待（忙等待） | 让出CPU，进入等待队列 |
| **CPU使用** | 100%占用（浪费） | 0%占用（高效） |
| **响应时间** | 极快（微秒级） | 较慢（毫秒级，需线程切换） |
| **等待队列** | 不使用 | 使用 `wait_queue` |
| **适用场景** | 临界区极短（<微秒） | 临界区较长（>毫秒） |
| **系统开销** | 无线程切换开销 | 线程切换+队列管理开销 |
| **创建函数** | `mutex_create()` | `mutex_blocking_create()` |

### 互斥锁ID的作用

**为什么需要 `next_mutex_id` 和 `mid`？**

1. **用户态引用**：
   - 用户程序通过 `mid`（整数）引用互斥锁
   - 避免直接暴露内核指针（安全考虑）

2. **快速索引**：
   - `mid` 直接作为数组索引：`mutex_pool[mid]`
   - O(1) 时间复杂度访问

3. **进程隔离**：
   - 每个进程有独立的 `mutex_pool`
   - 相同的 `mid` 在不同进程中指向不同的锁
   - 进程无法访问其他进程的锁

4. **资源限制**：
   - 最多创建 `LOCK_POOL_SIZE` 个锁
   - 超过限制时，`mutex_create()` 返回错误

### 与线程的关系

```
进程 p (pid=1)
├── next_mutex_id = 3  (已创建0,1,2三个锁)
├── mutex_pool[LOCK_POOL_SIZE]
│   ├── [0] mutex (blocking=1) - 被线程 tid=1 持有，等待队列 [tid=2, tid=3]
│   ├── [1] mutex (blocking=0) - 未锁定，线程 tid=4 正在自旋等待
│   ├── [2] mutex (blocking=1) - 未锁定，等待队列为空
│   └── [3..LOCK_POOL_SIZE-1] 未分配
└── threads[NTHREAD]
    ├── [0] thread (主线程，tid=0) - 运行中
    ├── [1] thread - 运行中，持有 mutex[0]
    ├── [2] thread - 睡眠中，在 mutex[0] 的等待队列中
    ├── [3] thread - 睡眠中，在 mutex[0] 的等待队列中
    └── [4] thread - 运行中，正在自旋等待 mutex[1]
```

### 关键设计决策

#### 1. 为什么使用固定大小数组？
- **简单性**：无需动态内存分配
- **确定性**：内存占用固定，不会耗尽
- **安全性**：避免内存泄漏和碎片

#### 2. 为什么互斥锁属于进程而非线程？
- **资源共享**：同一进程的多个线程需要共享互斥锁来同步
- **生命周期**：锁的生命周期通常与进程相同
- **全局访问**：任何线程都可以访问进程的任何锁

#### 3. 为什么使用静态分配的等待队列？
```c
int _wait_queue_data[WAIT_QUEUE_MAX_LENGTH];
```
- **避免内核动态内存分配**：内核中的动态分配复杂且容易出错
- **简化实现**：队列操作不涉及 `kmalloc/kfree`
- **限制等待线程数**：`WAIT_QUEUE_MAX_LENGTH` 防止无限增长

### 典型使用场景示例

```c
// 全局共享资源（进程内所有线程可见）
int shared_counter = 0;
int mutex_id;  // 互斥锁ID

void thread_function() {
    for (int i = 0; i < 1000; i++) {
        mutex_lock(mutex_id);  // 获取锁
        // --- 临界区开始 ---
        int old = shared_counter;
        shared_counter = old + 1;
        // --- 临界区结束 ---
        mutex_unlock(mutex_id);  // 释放锁
    }
    exit(0);
}

int main() {
    // 创建阻塞模式互斥锁
    mutex_id = mutex_blocking_create();

    // 创建3个线程，所有线程共享 mutex_id
    int tids[3];
    for (int i = 0; i < 3; i++) {
        tids[i] = thread_create(thread_function, NULL);
    }

    // 等待所有线程完成
    for (int i = 0; i < 3; i++) {
        waittid(tids[i]);
    }

    printf("Final counter: %d\n", shared_counter);  // 输出: Final counter: 3000
    return 0;
}
```

**执行流程**：
1. `main()` 创建 `mutex_id=0`（进程的 `next_mutex_id` 从0开始）
2. 3个线程并发执行，都尝试获取 `mutex_id=0`
3. 假设线程1先获取锁，线程2和3进入 `mutex_pool[0].wait_queue`
4. 线程1完成临界区后释放锁，唤醒线程2
5. 线程2获取锁，线程3继续等待
6. 线程2完成后释放锁，唤醒线程3
7. 线程3获取锁并完成
8. 最终 `shared_counter = 3000`（无竞态条件）

---

## 11. sys_mutex_create与mutex_create函数功能说明 (os/syscall.c, os/sync.c)

**作用**：创建一个新的互斥锁，返回互斥锁ID供用户程序使用

### 函数定义

#### 系统调用接口 (os/syscall.c)

```c
int sys_mutex_create(int blocking)  // 函数名：系统调用-创建互斥锁；参数blocking：0=非阻塞模式，1=阻塞模式
{
	struct mutex *m = mutex_create(blocking);  // 调用内核函数创建互斥锁，传入阻塞模式标志
	if (m == NULL) {  // 检查互斥锁创建是否失败
		return -1;  // 返回-1表示创建失败（可能是锁池耗尽）
	}
	int mutex_id = m - curr_proc()->mutex_pool;  // 通过指针减法计算互斥锁ID（数组索引）
	return mutex_id;  // 返回互斥锁ID给用户程序
}
```

#### 内核实现函数 (os/sync.c)

```c
struct mutex *mutex_create(int blocking)  // 函数名：创建互斥锁；参数blocking：阻塞模式标志；返回值：互斥锁指针或NULL
{
	struct proc *p = curr_proc();  // 获取当前进程的指针
	if (p->next_mutex_id >= LOCK_POOL_SIZE) {  // 检查是否已达到锁池上限
		return NULL;  // 返回NULL表示创建失败（锁资源耗尽）
	}
	struct mutex *m = &p->mutex_pool[p->next_mutex_id];  // 从锁池中获取下一个可用的互斥锁指针
	p->next_mutex_id++;  // 递增next_mutex_id，指向下一个可用槽位
	m->blocking = blocking;  // 设置阻塞模式标志（0或1）
	m->locked = 0;  // 初始化锁状态为未锁定
	if (blocking) {  // 如果是阻塞模式
		// blocking mutex need wait queue but spinning mutex not  // 阻塞模式需要等待队列，自旋模式不需要
		init_queue(&m->wait_queue, WAIT_QUEUE_MAX_LENGTH,  // 初始化等待队列
		    m->_wait_queue_data);  // 使用静态分配的数组作为队列存储空间
	}
	return m;  // 返回互斥锁指针
}
```

**工作原理**：

### 第一步：用户态调用系统调用

```c
// 用户程序
int mid = mutex_blocking_create();  // 或 mutex_create()
	↓
// 用户态封装函数 (usr/lib/syscall.c)
int mutex_blocking_create() {
	return syscall(SYS_mutex_create, 1);  // 参数1表示阻塞模式
}
	↓
// 系统调用入口 (os/syscall.c)
int sys_mutex_create(int blocking)
```

### 第二步：检查锁池资源

```c
struct proc *p = curr_proc();  // 获取当前进程控制块
if (p->next_mutex_id >= LOCK_POOL_SIZE) {  // 检查是否超出锁池大小
	return NULL;  // 锁池已满，创建失败
}
```

**示例**：
- 假设 `LOCK_POOL_SIZE = 16`
- 如果 `p->next_mutex_id = 16`（已经创建了16个锁）
- 条件 `16 >= 16` 为真，返回 `NULL`
- 系统调用返回 `-1` 给用户程序，表示创建失败

### 第三步：分配互斥锁槽位

```c
struct mutex *m = &p->mutex_pool[p->next_mutex_id];  // 从锁池中获取互斥锁指针
p->next_mutex_id++;  // ID计数器递增
```

**指针运算示例**：
```
假设：
- p->mutex_pool 的地址 = 0x8000
- p->next_mutex_id = 2  (表示这是第3个锁)
- sizeof(struct mutex) = 64 字节

计算过程：
m = &p->mutex_pool[2]
  = 0x8000 + 2 * 64
  = 0x8000 + 128
  = 0x8080

p->next_mutex_id++  // 变为3
```

### 第四步：初始化互斥锁字段

```c
m->blocking = blocking;  // 设置阻塞模式
m->locked = 0;  // 初始状态：未锁定
```

**阻塞模式 vs 非阻塞模式**：
- **blocking = 1**（阻塞模式）：
  - 线程无法获取锁时，进入等待队列睡眠
  - 需要 `wait_queue`，必须初始化

- **blocking = 0**（非阻塞/自旋模式）：
  - 线程无法获取锁时，自旋等待（忙等待）
  - 不需要 `wait_queue`，不初始化

### 第五步：初始化等待队列（仅阻塞模式）

```c
if (blocking) {  // 仅当阻塞模式时执行
	init_queue(&m->wait_queue, WAIT_QUEUE_MAX_LENGTH,
	    m->_wait_queue_data);
}
```

**为什么要条件初始化？**
- 非阻塞模式（自旋锁）不使用等待队列
- 节省初始化开销
- 避免浪费内存（虽然 `_wait_queue_data` 仍然占用空间）

**队列初始化**：
```c
// init_queue 伪代码
void init_queue(struct queue *q, int capacity, int *data) {
	q->head = 0;  // 队首指针
	q->tail = 0;  // 队尾指针
	q->size = 0;  // 当前元素数量
	q->capacity = capacity;  // 最大容量
	q->data = data;  // 指向静态分配的数组
}
```

### 第六步：计算互斥锁ID

```c
// 在 sys_mutex_create 中
int mutex_id = m - curr_proc()->mutex_pool;
return mutex_id;
```

**指针减法技巧**：
```
原理：
数组索引 = (指针地址 - 数组首地址) / 元素大小

示例：
m = 0x8080  (mutex_pool[2] 的地址)
mutex_pool = 0x8000  (数组首地址)
sizeof(struct mutex) = 64

计算：
mutex_id = (0x8080 - 0x8000) / 64
         = 128 / 64
         = 2  ✅

这就是为什么 m - mutex_pool 可以直接得到索引！
```

### 第七步：返回ID给用户程序

```c
return mutex_id;  // 返回整数ID
```

**完整调用链**：
```
用户程序：int mid = mutex_blocking_create();
    ↓
用户库：syscall(SYS_mutex_create, 1)
    ↓
内核入口：sys_mutex_create(1)
    ↓
内核函数：mutex_create(1)
    ↓
分配互斥锁：m = &p->mutex_pool[0], next_mutex_id++
    ↓
初始化字段：m->blocking=1, m->locked=0
    ↓
初始化队列：init_queue(&m->wait_queue, ...)
    ↓
计算ID：mutex_id = m - mutex_pool = 0
    ↓
返回：用户程序得到 mid = 0
```

### 函数执行时间线示例

**场景：创建3个互斥锁**

```
时刻T0: 进程启动
  p->next_mutex_id = 0
  mutex_pool[] 全部未初始化

时刻T1: 创建第1个锁（阻塞模式）
  sys_mutex_create(1)
    ↓
  mutex_create(1)
    ↓
  检查：0 >= 16? 否  ✅
    ↓
  m = &mutex_pool[0]  (地址0x8000)
  p->next_mutex_id++  → 变为1
    ↓
  m->blocking = 1
  m->locked = 0
  init_queue(&m->wait_queue, ...)  ✅
    ↓
  计算 ID：0x8000 - 0x8000 = 0
    ↓
  返回：mid = 0

时刻T2: 创建第2个锁（阻塞模式）
  sys_mutex_create(1)
    ↓
  检查：1 >= 16? 否  ✅
    ↓
  m = &mutex_pool[1]  (地址0x8040)
  p->next_mutex_id++  → 变为2
    ↓
  初始化字段...
  init_queue(...)
    ↓
  计算 ID：(0x8040 - 0x8000) / 64 = 1
    ↓
  返回：mid = 1

时刻T3: 创建第3个锁（非阻塞模式）
  sys_mutex_create(0)
    ↓
  检查：2 >= 16? 否  ✅
    ↓
  m = &mutex_pool[2]  (地址0x8080)
  p->next_mutex_id++  → 变为3
    ↓
  m->blocking = 0
  m->locked = 0
  init_queue(...)  ❌ 不执行（blocking=0）
    ↓
  计算 ID：(0x8080 - 0x8000) / 64 = 2
    ↓
  返回：mid = 2

最终状态：
  p->next_mutex_id = 3
  mutex_pool[0]: blocking=1, 已初始化队列
  mutex_pool[1]: blocking=1, 已初始化队列
  mutex_pool[2]: blocking=0, 未初始化队列
  mutex_pool[3..15]: 未使用
```

### 错误处理

#### 1. 锁池耗尽

```c
if (p->next_mutex_id >= LOCK_POOL_SIZE) {
	return NULL;
}
```

**示例**：
```
LOCK_POOL_SIZE = 16
已创建16个锁（next_mutex_id = 16）

尝试创建第17个锁：
  sys_mutex_create(1)
    ↓
  mutex_create(1)
    ↓
  检查：16 >= 16? 是  ❌
    ↓
  返回 NULL
    ↓
  sys_mutex_create 返回 -1
    ↓
  用户程序得到 -1（创建失败）
```

**用户程序应如何处理**：
```c
int mid = mutex_blocking_create();
if (mid < 0) {
	printf("Failed to create mutex: pool exhausted\n");
	exit(-1);
}
```

### 关键设计亮点

#### 1. 指针减法计算ID

```c
int mutex_id = m - curr_proc()->mutex_pool;
```

**优点**：
- **快速**：O(1)时间复杂度，无需搜索
- **准确**：直接得到数组索引
- **简洁**：一行代码完成计算

**原理**：
```c
// C语言的指针减法规则
pointer_a - pointer_b = (地址差) / sizeof(类型)

示例：
mutex_pool[2] - mutex_pool[0]
= (0x8080 - 0x8000) / 64
= 128 / 64
= 2
```

#### 2. 条件队列初始化

```c
if (blocking) {
	init_queue(&m->wait_queue, WAIT_QUEUE_MAX_LENGTH,
	    m->_wait_queue_data);
}
```

**优势**：
- **节省时间**：跳过不必要的初始化
- **明确语义**：阻塞模式才需要等待队列
- **类型安全**：编译时确定行为

#### 3. 锁池预分配

```c
struct mutex mutex_pool[LOCK_POOL_SIZE];
```

**优势**：
- **确定性**：固定内存占用，不会耗尽
- **简单性**：无需动态内存分配
- **安全性**：避免内存泄漏和碎片

### 与用户程序的接口

#### 用户态封装函数

```c
// usr/lib/syscall.c

// 创建非阻塞互斥锁（自旋锁）
int mutex_create() {
	return syscall(SYS_mutex_create, 0);  // 参数0
}

// 创建阻塞互斥锁
int mutex_blocking_create() {
	return syscall(SYS_mutex_create, 1);  // 参数1
}
```

#### 用户程序使用示例

```c
// user/src/ch8b_mut_race.c

int mutex_id;  // 全局互斥锁ID

int main() {
	// 创建阻塞模式互斥锁
	assert((mutex_id = mutex_blocking_create()) >= 0);  // 确保创建成功

	// 创建多个线程，共享 mutex_id
	for (int i = 0; i < thread_count; i++) {
		threads[i] = thread_create(fun, (void *)i);
	}

	// 使用互斥锁保护共享资源
	void fun(long i) {
		for (int j = 0; j < per_thread; j++) {
			mutex_lock(mutex_id);  // 获取锁
			// 临界区
			a++;
			mutex_unlock(mutex_id);  // 释放锁
		}
		exit(0);
	}
}
```

### 性能分析

#### 时间复杂度

| 操作 | 时间复杂度 | 说明 |
|------|----------|------|
| 锁池检查 | O(1) | 简单比较 |
| 指针运算 | O(1) | 数组索引访问 |
| 字段初始化 | O(1) | 赋值操作 |
| 队列初始化 | O(1) | 设置队列元数据 |
| ID计算 | O(1) | 指针减法 |
| **总计** | **O(1)** | **常数时间** |

#### 空间复杂度

| 资源 | 大小 | 说明 |
|------|------|------|
| 互斥锁结构体 | ~64字节 | 每个锁 |
| 等待队列数据 | WAIT_QUEUE_MAX_LENGTH × 4字节 | 仅阻塞模式 |
| 锁池总大小 | LOCK_POOL_SIZE × 64字节 | 固定分配 |

**示例**（假设 LOCK_POOL_SIZE=16, WAIT_QUEUE_MAX_LENGTH=16）：
```
每个阻塞锁：64 + 16×4 = 128 字节
每个非阻塞锁：64 字节
锁池总大小：16 × 128 = 2048 字节（全部为阻塞锁）
```

### 内存状态可视化

**创建3个锁后的内存状态**：

```
进程 p 的互斥锁池 (mutex_pool[16])
地址       索引  blocking  locked  wait_queue
─────────────────────────────────────────
0x8000     [0]     1        0       [初始化]
0x8040     [1]     1        0       [初始化]
0x8080     [2]     0        0       [未使用]
0x80C0     [3]     -        -       [未分配]
0x8100     [4]     -        -       [未分配]
...
0x8FC0    [15]     -        -       [未分配]

p->next_mutex_id = 3
下一个可用槽位：[3]
```

### 与其他系统调用的配合

**创建锁 → 使用锁 → 销毁锁**（简化版）：

```c
// 1. 创建锁
int mid = mutex_blocking_create();  // sys_mutex_create(1)

// 2. 使用锁
mutex_lock(mid);   // sys_mutex_lock(mid)
// ... 临界区 ...
mutex_unlock(mid); // sys_mutex_unlock(mid)

// 3. 注意：uCore通常不提供显式销毁锁的系统调用
// 锁会随着进程退出自动释放
```

### 测试与验证

**如何测试互斥锁创建**：

```c
// 测试1：创建单个锁
int mid = mutex_blocking_create();
assert(mid >= 0);  // 应该成功
assert(mid == 0);  // 第一个锁ID应该是0

// 测试2：创建多个锁
int mids[16];
for (int i = 0; i < 16; i++) {
	mids[i] = mutex_blocking_create();
	assert(mids[i] == i);  // ID应该递增
}

// 测试3：锁池耗尽
int mid_fail = mutex_blocking_create();
assert(mid_fail < 0);  // 应该失败

// 测试4：非阻塞模式
int spin_mid = mutex_create();
assert(spin_mid >= 0);
// 验证不初始化等待队列（通过行为观察）
```

---

## 12. sys_mutex_unlock与mutex_unlock函数功能说明 (os/syscall.c, os/sync.c)

**作用**：释放互斥锁，唤醒等待队列中的线程（阻塞模式）或直接解锁（非阻塞模式）

### 函数定义

#### 系统调用接口 (os/syscall.c)

```c
int sys_mutex_unlock(int mutex_id)  // 函数名：系统调用-释放互斥锁；参数mutex_id：要释放的互斥锁ID
{
	if (mutex_id < 0 || mutex_id >= curr_proc()->next_mutex_id) {  // 检查mutex_id是否在有效范围内
		return -1;  // 返回-1表示参数错误（无效的mutex_id）
	}
	mutex_unlock(&curr_proc()->mutex_pool[mutex_id]);  // 通过ID获取互斥锁指针，调用内核函数释放锁
	return 0;  // 返回0表示成功释放锁
}
```

#### 内核实现函数 (os/sync.c)

```c
void mutex_unlock(struct mutex *m)  // 函数名：释放互斥锁；参数m：互斥锁指针
{
	if (m->blocking) {  // 如果是阻塞模式（需要管理等待队列）
		struct thread *t = id_to_task(pop_queue(&m->wait_queue));  // 从等待队列中取出一个等待的线程
		if (t == NULL) {  // 如果等待队列为空
			// Without waiting thread, just release the lock  // 没有线程在等待，直接释放锁
			m->locked = 0;  // 设置锁状态为未锁定
		} else {  // 如果有线程在等待
			// Or we should give lock to next thread  // 将锁传递给下一个等待的线程
			t->state = RUNNABLE;  // 设置线程状态为就绪
			add_task(t);  // 将线程加入调度队列，准备运行
		}
	} else {  // 非阻塞模式（自旋锁）
		m->locked = 0;  // 直接设置锁状态为未锁定，无需管理等待队列
	}
}
```

**工作原理**：

### 第一步：用户态调用系统调用

```c
// 用户程序
mutex_unlock(mutex_id);
	↓
// 用户态封装函数 (usr/lib/syscall.c)
int mutex_unlock(int mid) {
	return syscall(SYS_mutex_unlock, mid);
}
	↓
// 系统调用入口 (os/syscall.c)
int sys_mutex_unlock(int mutex_id)
```

### 第二步：参数验证（mutex_id有效性检查）

```c
if (mutex_id < 0 || mutex_id >= curr_proc()->next_mutex_id) {
	return -1;
}
```

**验证逻辑**：
```
有效范围：[0, next_mutex_id - 1]

示例1：有效ID
  next_mutex_id = 3  (已创建0,1,2三个锁)
  mutex_id = 1
  检查：1 < 0? 否 ✅
       1 >= 3? 否 ✅
  → 通过验证

示例2：负数ID
  mutex_id = -1
  检查：-1 < 0? 是 ❌
  → 返回 -1（参数错误）

示例3：超出范围的ID
  next_mutex_id = 3
  mutex_id = 5
  检查：5 < 0? 否 ✅
       5 >= 3? 是 ❌
  → 返回 -1（参数错误）

示例4：未使用的ID
  next_mutex_id = 3  (已分配0,1,2)
  mutex_id = 2
  检查：2 < 0? 否 ✅
       2 >= 3? 否 ✅
  → 通过验证（虽然锁2可能未被使用）
```

**为什么用 `next_mutex_id` 而不是 `LOCK_POOL_SIZE`？**
- `next_mutex_id` 是动态上界，表示实际已分配的最大ID+1
- 使用 `LOCK_POOL_SIZE` 会允许访问未初始化的锁
- 当前设计允许"空洞"（例如：创建了0,2，但1未创建）

### 第三步：获取互斥锁指针

```c
mutex_unlock(&curr_proc()->mutex_pool[mutex_id]);
```

**指针计算**：
```
假设：
  mutex_id = 2
  curr_proc()->mutex_pool = 0x8000
  sizeof(struct mutex) = 64

计算：
  &mutex_pool[2]
= 0x8000 + 2 * 64
= 0x8000 + 128
= 0x8080  ← 互斥锁指针 m
```

### 第四步：判断阻塞模式

```c
if (m->blocking) {  // 检查互斥锁的阻塞模式标志
	// 阻塞模式：需要管理等待队列
} else {
	// 非阻塞模式：直接解锁
}
```

**两种模式的处理差异**：
| 模式 | m->blocking | 等待队列 | 处理方式 |
|------|------------|---------|---------|
| 阻塞模式 | 1 | 有等待队列 | 需要唤醒线程 |
| 非阻塞模式 | 0 | 无等待队列 | 直接解锁 |

### 第五步：阻塞模式处理路径

#### 5a. 从等待队列取出线程

```c
struct thread *t = id_to_task(pop_queue(&m->wait_queue));
```

**pop_queue操作**（队列出队）：
```
等待队列状态（假设）：
  wait_queue = [tid=2, tid=3, tid=5]
  head = 0, tail = 3

pop_queue(&m->wait_queue):
  1. 取出队首元素：tid=2
  2. head = 1  (队首指针后移)
  3. 返回 tid=2

等待队列变为：
  wait_queue = [tid=3, tid=5]
  head = 1, tail = 3
```

**id_to_task函数**：
```c
// 伪代码：将线程ID转换为线程控制块指针
struct thread *id_to_task(int tid) {
	struct proc *p = curr_proc();
	if (tid < 0 || tid >= NTHREAD) {
		return NULL;
	}
	return &p->threads[tid];  // 返回线程指针
}
```

#### 5b. 检查是否有线程在等待

```c
if (t == NULL) {  // pop_queue返回NULL，表示队列为空
	// 没有线程在等待
	m->locked = 0;  // 直接解锁
} else {  // 有线程在等待
	// 将锁传递给下一个线程
	t->state = RUNNABLE;
	add_task(t);
}
```

**场景1：等待队列为空**

```
初始状态：
  m->locked = 1  (被线程A持有)
  m->wait_queue = [空]

线程A执行 mutex_unlock():
  pop_queue() → 返回 NULL
  t = NULL
  → 执行 if (t == NULL) 分支
  → m->locked = 0  ✅

最终状态：
  m->locked = 0  (锁被释放)
  m->wait_queue = [空]
```

**场景2：等待队列非空**

```
初始状态：
  m->locked = 1  (被线程A持有)
  m->wait_queue = [tid=2, tid=3]

线程A执行 mutex_unlock():
  pop_queue() → 返回 tid=2
  t = &threads[2]
  → 执行 else 分支
  → t->state = RUNNABLE  (设置线程2为就绪状态)
  → add_task(t)  (将线程2加入调度队列)
  → 注意：m->locked 保持为 1！ ⚠️

最终状态：
  m->locked = 1  (锁被传递给线程2)
  m->wait_queue = [tid=3]
  threads[2].state = RUNNABLE  (线程2被唤醒)
```

**关键问题：为什么保持 `locked = 1`？**

这是因为采用了**锁传递机制**（Lock Passing）：
- 线程A释放锁时，不直接解锁
- 而是将锁"传递"给线程2
- 线程2被唤醒时，不需要再次获取锁（因为它已被标记为持有者）
- 这样避免了"唤醒后立即竞争锁"的低效情况

**但是，这种实现可能不完整** ⚠️
- 标准做法：线程2被唤醒后，会检查自己是否持有锁
- 或者：在唤醒时就明确标记锁的所有者

#### 5c. 唤醒等待的线程

```c
t->state = RUNNABLE;  // 设置线程状态为就绪
add_task(t);  // 将线程加入调度队列
```

**状态转换**：
```
线程状态变化：
  SLEEPING（睡眠，在等待队列中）
    ↓
  RUNNABLE（就绪，加入调度队列）
    ↓
  RUNNING（运行，被调度器选中）
```

**add_task函数**（伪代码）：
```c
void add_task(struct thread *t) {
	// 将线程加入全局调度队列
	enqueue(&run_queue, t);
	// 调度器会在下一次调度时考虑这个线程
}
```

### 第六步：非阻塞模式处理路径

```c
} else {  // 非阻塞模式（自旋锁）
	m->locked = 0;  // 直接设置锁状态为未锁定
}
```

**为什么非阻塞模式不需要管理等待队列？**

```
非阻塞模式的特性：
- 线程获取锁失败时，会自旋等待（忙等待）
- 自旋等待的线程不会进入等待队列
- 所有等待的线程都在CPU上循环检查 m->locked
- 因此，释放锁时只需要设置 m->locked = 0
- 自旋的线程会发现锁已释放，其中一个会成功获取锁
```

**自旋等待的伪代码**：
```c
// 非阻塞模式的 mutex_lock 实现
void mutex_lock_spin(struct mutex *m) {
	while (atomic_swap(&m->locked, 1) == 1) {
		// 自旋：循环检查锁状态
		// 注意：实际实现会使用原子操作
	}
}
```

### 完整执行流程示例

#### 示例1：阻塞模式，无等待线程

```
初始状态：
  线程A持有锁 mid=0
  m->locked = 1
  m->blocking = 1
  m->wait_queue = [空]

时刻T0: 线程A执行 mutex_unlock(0)
	↓
sys_mutex_unlock(0)
	检查：0 < 0? 否 ✅
	     0 >= 1? 否 ✅
	↓
mutex_unlock(&mutex_pool[0])
	检查：m->blocking == 1? 是
	↓
	pop_queue(&m->wait_queue) → 返回 NULL
	t = NULL
	↓
	if (t == NULL) → 执行
		m->locked = 0  ✅
	↓
返回：0

最终状态：
  m->locked = 0  (锁被释放)
  m->wait_queue = [空]
```

#### 示例2：阻塞模式，有等待线程

```
初始状态：
  线程A持有锁 mid=0
  m->locked = 1
  m->blocking = 1
  m->wait_queue = [tid=2, tid=3]
  线程2、3在等待队列中睡眠

时刻T0: 线程A执行 mutex_unlock(0)
	↓
pop_queue(&m->wait_queue) → 返回 2
	↓
id_to_task(2) → 返回 &threads[2]
t = &threads[2]
	↓
	if (t == NULL)? 否
	↓
执行 else 分支：
	t->state = RUNNABLE  (线程2被唤醒)
	add_task(t)  (线程2加入调度队列)
	↓
返回：0

最终状态：
  m->locked = 1  (保持锁定，传递给线程2) ⚠️
  m->wait_queue = [tid=3]
  threads[2].state = RUNNABLE
  threads[3].state = SLEEPING
```

**后续流程**：
```
时刻T1: 调度器选择线程2运行
	线程2从 SLEEPING → RUNNING
	线程2继续执行（从阻塞点恢复）
	↓
时刻T2: 线程2执行临界区代码
	（持有锁 mid=0）
	↓
时刻T3: 线程2执行 mutex_unlock(0)
	唤醒线程3...
```

#### 示例3：非阻塞模式（自旋锁）

```
初始状态：
  线程A持有锁 mid=1
  m->locked = 1
  m->blocking = 0  (非阻塞模式)
  线程B、C正在自旋等待（在CPU上循环）

时刻T0: 线程A执行 mutex_unlock(1)
	↓
sys_mutex_unlock(1)
	检查：1 < 0? 否 ✅
	     1 >= 2? 否 ✅
	↓
mutex_unlock(&mutex_pool[1])
	检查：m->blocking == 1? 否
	↓
执行 else 分支：
	m->locked = 0  ✅
	↓
返回：0

最终状态：
  m->locked = 0  (锁被释放)
  无等待队列管理

同时，线程B和C的自旋循环会发现：
  while (m->locked == 1) { ... }
  // m->locked 变为0，循环退出
  其中一个线程（假设B）成功设置 m->locked = 1
  线程B获取锁，线程C继续自旋
```

### 锁传递机制详解

**为什么阻塞模式不直接设置 `locked = 0`？**

#### 错误的做法（直接解锁）：

```c
void mutex_unlock_BAD(struct mutex *m) {
	if (m->blocking) {
		struct thread *t = id_to_task(pop_queue(&m->wait_queue));
		if (t != NULL) {
			t->state = RUNNABLE;
			add_task(t);
		}
		m->locked = 0;  // ⚠️ 直接解锁
	} else {
		m->locked = 0;
	}
}
```

**问题**：
1. 线程A释放锁（`locked = 0`）
2. 线程B被唤醒（`state = RUNNABLE`）
3. 但线程B还未运行，其他线程C可能抢先获取锁
4. 线程B最终运行时，需要重新竞争锁（可能失败）

#### 正确的做法（锁传递）：

```c
void mutex_unlock_GOOD(struct mutex *m) {
	if (m->blocking) {
		struct thread *t = id_to_task(pop_queue(&m->wait_queue));
		if (t == NULL) {
			m->locked = 0;  // 无等待线程，直接解锁
		} else {
			t->state = RUNNABLE;
			add_task(t);
			// m->locked 保持为 1，锁传递给线程t
		}
	} else {
		m->locked = 0;  // 自旋锁直接解锁
	}
}
```

**优势**：
1. 线程A释放锁时，明确指定下一个持有者（线程B）
2. 线程B被唤醒后，无需竞争锁，直接持有
3. 避免了"惊群效应"（多个线程同时竞争）

**但是需要注意**：当前代码片段中，被唤醒的线程需要知道它已经持有锁。
- 可能的实现：线程控制块中有 `holding_lock` 字段
- 或者：被唤醒的线程不会重新检查锁状态

### 与mutex_lock的配合

**完整的锁使用流程**：

```c
// 线程A
mutex_lock(mid);    // 获取锁
// ... 临界区 ...
mutex_unlock(mid);  // 释放锁

// 线程B（同时竞争）
mutex_lock(mid);    // 尝试获取锁，失败
                   // 进入等待队列，睡眠
                   // 被唤醒后继续执行
// ... 临界区 ...
mutex_unlock(mid);  // 释放锁
```

**状态机**：

```
锁的状态转换：
LOCKED (线程A) → LOCKED (线程B) → LOCKED (线程C) → UNLOCKED

线程的状态转换：
线程A: RUNNING → LOCKED → CRITICAL → UNLOCKING → RUNNABLE
线程B: RUNNING → WAITING → SLEEPING → RUNNABLE → LOCKED → CRITICAL
线程C: RUNNING → WAITING → SLEEPING → ...
```

### 参数验证的重要性

**为什么需要参数验证？**

```c
if (mutex_id < 0 || mutex_id >= curr_proc()->next_mutex_id) {
	return -1;
}
```

**防止的错误**：

#### 1. 负数ID
```c
mutex_unlock(-1);
// &mutex_pool[-1] → 访问非法内存！
// 可能导致段错误或内核崩溃
```

#### 2. 超出范围的ID
```c
mutex_id = 100;
next_mutex_id = 3;
mutex_unlock(100);
// &mutex_pool[100] → 访问越界内存！
// 可能读取其他进程的数据
```

#### 3. 未初始化的锁
```c
next_mutex_id = 3;  // 已创建0,1,2
mutex_unlock(2);    // 锁2已创建，但可能从未初始化
// m->blocking 可能是垃圾值
// m->locked 可能是随机值
```

**更好的验证方案**（建议）：
```c
// 方案1：添加 initialized 标志
struct mutex {
	uint initialized;  // 0=未初始化，1=已初始化
	uint blocking;
	uint locked;
	...
};

// 创建时设置
m->initialized = 1;

// 释放时检查
if (!m->initialized) {
	return -1;
}

// 方案2：使用位图跟踪已分配的锁
uint mutex_allocated_bitmap[LOCK_POOL_SIZE / 32];
// 每个位对应一个锁是否已分配
```

### 函数执行时间线示例（多线程场景）

**场景：3个线程竞争1个锁**

```
初始状态：
  锁 mid=0: locked=0, wait_queue=[]
  线程A (tid=0): RUNNING
  线程B (tid=1): RUNNABLE
  线程C (tid=2): RUNNABLE

时刻T0: 线程A获取锁
  mutex_lock(0) → m->locked = 1
  锁: locked=1

时刻T1: 线程B尝试获取锁
  mutex_lock(0) → m->locked == 1
  进入等待队列
  锁: wait_queue=[tid=1]
  线程B: SLEEPING

时刻T2: 线程C尝试获取锁
  mutex_lock(0) → m->locked == 1
  进入等待队列
  锁: wait_queue=[tid=1, tid=2]
  线程C: SLEEPING

时刻T3: 线程A释放锁
  mutex_unlock(0)
  pop_queue() → 返回 tid=1
  线程B: RUNNABLE
  锁: wait_queue=[tid=2], locked=1 (传递给线程B)

时刻T4: 调度器选择线程B运行
  线程B: RUNNING
  线程B执行临界区

时刻T5: 线程B释放锁
  mutex_unlock(0)
  pop_queue() → 返回 tid=2
  线程C: RUNNABLE
  锁: wait_queue=[], locked=1 (传递给线程C)

时刻T6: 调度器选择线程C运行
  线程C: RUNNING
  线程C执行临界区

时刻T7: 线程C释放锁
  mutex_unlock(0)
  pop_queue() → 返回 NULL
  锁: locked=0  (无等待线程，直接解锁)
```

### 性能分析

#### 时间复杂度

| 操作 | 时间复杂度 | 说明 |
|------|----------|------|
| 参数验证 | O(1) | 范围检查 |
| 指针获取 | O(1) | 数组索引 |
| 队列出队 | O(1) | FIFO队列操作 |
| 线程唤醒 | O(1) | 状态设置+加入调度队列 |
| **总计** | **O(1)** | **常数时间** |

#### 空间复杂度

| 资源 | 空间 | 说明 |
|------|------|------|
| 等待队列 | O(n) | n=等待线程数 |
| 线程控制块 | O(1) | 已存在，无需额外分配 |

**对比两种模式**：
```
阻塞模式：
  时间：O(1)（队列操作）
  空间：O(n)（等待队列）
  CPU：低（线程睡眠，不占用CPU）

非阻塞模式：
  时间：O(1)（直接解锁）
  空间：O(1)（无等待队列）
  CPU：高（自旋线程占用CPU）
```

### 常见错误与调试

#### 错误1：释放未持有的锁

```c
// 线程A
mutex_lock(0);  // 获取锁0
mutex_unlock(1);  // ⚠️ 错误：释放了锁1而不是锁0
```

**后果**：
- 锁0未被释放（死锁）
- 锁1可能被错误释放（破坏互斥性）

#### 错误2：重复释放

```c
// 线程A
mutex_lock(0);
mutex_unlock(0);
mutex_unlock(0);  // ⚠️ 错误：重复释放
```

**当前代码的问题**：
- 不会检查锁的持有者
- 可能导致多个线程同时进入临界区

**改进方案**：
```c
// 添加 owner 字段
struct mutex {
	uint locked;
	struct thread *owner;  // 当前持有锁的线程
	...
};

// 释放时检查
if (m->owner != curr_thread()) {
	return -1;  // 错误：不是锁的持有者
}
m->owner = NULL;  // 清空持有者
m->locked = 0;
```

#### 错误3：忘记释放锁

```c
// 线程A
mutex_lock(0);
if (error_condition) {
	return -1;  // ⚠️ 错误：提前返回，忘记释放锁
}
mutex_unlock(0);
```

**后果**：
- 其他线程永久等待（死锁）

**解决方案**：
```c
// 方案1：使用 goto
mutex_lock(0);
if (error_condition) {
	ret = -1;
	goto cleanup;
}
ret = 0;
cleanup:
mutex_unlock(0);
return ret;

// 方案2：使用 RAII（C++）
std::lock_guard<std::mutex> lock(mutex);
// 自动释放
```

### 测试与验证

**测试用例**：

```c
// 测试1：基本释放
int mid = mutex_blocking_create();
mutex_lock(mid);
assert(mutex_unlock(mid) == 0);  // 应该成功

// 测试2：重复释放
assert(mutex_unlock(mid) == -1);  // 应该失败（或未定义行为）

// 测试3：释放未持有的锁
int mid2 = mutex_blocking_create();
assert(mutex_unlock(mid2) == -1);  // 应该失败

// 测试4：无效ID
assert(mutex_unlock(-1) == -1);  // 应该失败
assert(mutex_unlock(999) == -1);  // 应该失败

// 测试5：唤醒等待线程
int mid3 = mutex_blocking_create();
mutex_lock(mid3);
int tid = thread_create(worker, NULL);
mutex_unlock(mid3);  // 应该唤醒 worker 线程
waittid(tid);

// 测试6：非阻塞模式
int spin_mid = mutex_create();
mutex_lock(spin_mid);
mutex_unlock(spin_mid);  // 应该立即返回
```

---


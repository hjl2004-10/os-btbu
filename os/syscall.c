#include "console.h"
#include "defs.h"
#include "loader.h"
#include "sync.h"
#include "syscall.h"
#include "syscall_ids.h"
#include "timer.h"
#include "trap.h"
#include "ai_sched.h"

uint64 console_write(uint64 va, uint64 len)
{
	struct proc *p = curr_proc();
	char str[MAX_STR_LEN];
	int size = copyinstr(p->pagetable, str, va, MIN(len, MAX_STR_LEN));
	tracef("write size = %d", size);
	for (int i = 0; i < size; ++i) {
		console_putchar(str[i]);
	}
	return len;
}

uint64 console_read(uint64 va, uint64 len)
{
	struct proc *p = curr_proc();
	char str[MAX_STR_LEN];
	tracef("read size = %d", len);
	for (int i = 0; i < len; ++i) {
		int c = consgetc();
		str[i] = c;
	}
	copyout(p->pagetable, va, str, len);
	return len;
}

uint64 sys_write(int fd, uint64 va, uint64 len)
{
	if (fd < 0 || fd > FD_BUFFER_SIZE)
		return -1;
	struct proc *p = curr_proc();
	struct file *f = p->files[fd];
	if (f == NULL) {
		errorf("invalid fd %d\n", fd);
		return -1;
	}
	switch (f->type) {
	case FD_STDIO:
		return console_write(va, len);
	case FD_PIPE:
		return pipewrite(f->pipe, va, len);
	case FD_INODE:
		return inodewrite(f, va, len);
	default:
		panic("unknown file type %d\n", f->type);
	}
}

uint64 sys_read(int fd, uint64 va, uint64 len)
{
	if (fd < 0 || fd > FD_BUFFER_SIZE)
		return -1;
	struct proc *p = curr_proc();
	struct file *f = p->files[fd];
	if (f == NULL) {
		errorf("invalid fd %d\n", fd);
		return -1;
	}
	switch (f->type) {
	case FD_STDIO:
		return console_read(va, len);
	case FD_PIPE:
		return piperead(f->pipe, va, len);
	case FD_INODE:
		return inoderead(f, va, len);
	default:
		panic("unknown file type %d\n", f->type);
	}
}

__attribute__((noreturn)) void sys_exit(int code)
{
	exit(code);
	__builtin_unreachable();
}

uint64 sys_sched_yield()
{
	yield();
	return 0;
}

uint64 sys_gettimeofday(uint64 val, int _tz)
{
	struct proc *p = curr_proc();
	uint64 cycle = get_cycle();
	TimeVal t;
	t.sec = cycle / CPU_FREQ;
	t.usec = (cycle % CPU_FREQ) * 1000000 / CPU_FREQ;
	copyout(p->pagetable, val, (char *)&t, sizeof(TimeVal));
	return 0;
}

uint64 sys_getpid()
{
	return curr_proc()->pid;
}

uint64 sys_getppid()
{
	struct proc *p = curr_proc();
	return p->parent == NULL ? IDLE_PID : p->parent->pid;
}

uint64 sys_clone()
{
	debugf("fork!");
	return fork();
}

static inline uint64 fetchaddr(pagetable_t pagetable, uint64 va)
{
	uint64 *addr = (uint64 *)useraddr(pagetable, va);
	return *addr;
}

uint64 sys_exec(uint64 path, uint64 uargv)
{
	struct proc *p = curr_proc();
	char name[MAX_STR_LEN];
	copyinstr(p->pagetable, name, path, MAX_STR_LEN);
	uint64 arg;
	static char strpool[MAX_ARG_NUM][MAX_STR_LEN];
	char *argv[MAX_ARG_NUM];
	int i;
	for (i = 0; uargv && (arg = fetchaddr(p->pagetable, uargv));
	     uargv += sizeof(char *), i++) {
		copyinstr(p->pagetable, (char *)strpool[i], arg, MAX_STR_LEN);
		argv[i] = (char *)strpool[i];
	}
	argv[i] = NULL;
	return exec(name, (char **)argv);
}

uint64 sys_wait(int pid, uint64 va)
{
	struct proc *p = curr_proc();
	int *code = (int *)useraddr(p->pagetable, va);
	return wait(pid, code);
}

uint64 sys_pipe(uint64 fdarray)
{
	struct proc *p = curr_proc();
	uint64 fd0, fd1;
	struct file *f0, *f1;
	f0 = filealloc();
	f1 = filealloc();
	if (f0 == NULL || f1 == NULL) {
		if (f0) fileclose(f0);
		if (f1) fileclose(f1);
		return -1;
	}
	if (pipealloc(f0, f1) < 0)
		goto err0;
	fd0 = fdalloc(f0);
	fd1 = fdalloc(f1);
	if (fd0 < 0 || fd1 < 0)
		goto err0;
	/* ch11: 修复：用户态传入int[2]数组，每个元素4字节 */
	int fd0_int = (int)fd0;
	int fd1_int = (int)fd1;
	if (copyout(p->pagetable, fdarray, (char *)&fd0_int, sizeof(int)) < 0 ||
	    copyout(p->pagetable, fdarray + sizeof(int), (char *)&fd1_int,
		    sizeof(int)) < 0) {
		goto err1;
	}
	return 0;

err1:
	p->files[fd0] = 0;
	p->files[fd1] = 0;
err0:
	fileclose(f0);
	fileclose(f1);
	return -1;
}

uint64 sys_openat(uint64 va, uint64 omode, uint64 _flags)
{
	struct proc *p = curr_proc();
	char path[200];
	copyinstr(p->pagetable, path, va, 200);
	return fileopen(path, omode);
}

uint64 sys_close(int fd)
{
	if (fd < 0 || fd > FD_BUFFER_SIZE)
		return -1;
	struct proc *p = curr_proc();
	struct file *f = p->files[fd];
	if (f == NULL) {
		errorf("invalid fd %d", fd);
		return -1;
	}
	fileclose(f);
	p->files[fd] = 0;
	return 0;
}

int sys_thread_create(uint64 entry, uint64 arg)
{
	struct proc *p = curr_proc();
	int tid = allocthread(p, entry, 1);
	if (tid < 0) {
		errorf("fail to create thread");
		return -1;
	}
	struct thread *t = &p->threads[tid];
	t->trapframe->a0 = arg;
	t->state = RUNNABLE;
	add_task(t);
	return tid;
}

int sys_gettid()
{
	return curr_thread()->tid;
}

int sys_waittid(int tid)
{
	if (tid < 0 || tid >= NTHREAD) {
		errorf("unexpected tid %d", tid);
		return -1;
	}
	struct thread *t = &curr_proc()->threads[tid];
	if (t->state == T_UNUSED || tid == curr_thread()->tid) {
		return -1;
	}
	if (t->state != EXITED) {
		return -2;
	}
	memset((void *)t->kstack, 7, KSTACK_SIZE);
	t->tid = -1;
	t->state = T_UNUSED;
	return t->exit_code;
}

/*
*	LAB5: (3) In the TA's reference implementation, here defines funtion
*					int deadlock_detect(const int available[LOCK_POOL_SIZE],
*						const int allocation[NTHREAD][LOCK_POOL_SIZE],
*						const int request[NTHREAD][LOCK_POOL_SIZE])
*				for both mutex and semaphore detect, you can also
*				use this idea or just ignore it.
*/
int deadlock_detect(struct proc *p)
{
	int work_mutex[LOCK_POOL_SIZE];
	int work_sem[LOCK_POOL_SIZE];
	int finish[NTHREAD];
	int i, j;

	// 初始化 Work = Available
	// Work 向量表示系统当前可提供的资源
	for (i = 0; i < LOCK_POOL_SIZE; i++) {
		// 如果 mutex 被锁住，则可用资源为 0，否则为 1
		work_mutex[i] = p->mutex_pool[i].locked ? 0 : 1;
		// 信号量的可用资源：count >= 0 时为 count，count < 0 时为 0
		work_sem[i] = p->semaphore_pool[i].count > 0 ?
				      p->semaphore_pool[i].count :
				      0;
	}

	// 初始化 Finish
	// Finish 向量表示线程是否可以完成运行
	for (i = 0; i < NTHREAD; i++) {
		// 未使用或已退出的线程视为已完成
		finish[i] = (p->threads[i].state == T_UNUSED ||
			     p->threads[i].state == EXITED) ?
				    1 :
				    0;
	}

	while (1) {
		int found = 0;
		for (i = 0; i < NTHREAD; i++) {
			if (!finish[i]) {
				int possible = 1;
				// 检查 mutex 需求是否满足 Request <= Work
				for (j = 0; j < LOCK_POOL_SIZE; j++) {
					if (p->mutex_request[i][j] >
					    work_mutex[j]) {
						possible = 0;
						break;
					}
				}
				if (!possible)
					continue;

				// 检查 semaphore 需求是否满足
				for (j = 0; j < LOCK_POOL_SIZE; j++) {
					if (p->sem_request[i][j] >
					    work_sem[j]) {
						possible = 0;
						break;
					}
				}

				if (possible) {
					// 假设线程 i 获得资源并运行结束，释放其持有的所有资源
					// Work = Work + Allocation
					for (j = 0; j < LOCK_POOL_SIZE; j++) {
						work_mutex[j] +=
							p->mutex_allocation[i]
									   [j];
						work_sem[j] +=
							p->sem_allocation[i][j];
					}
					finish[i] = 1;
					found = 1;
				}
			}
		}
		// 如果一轮循环中没有找到任何可满足的线程，则跳出
		if (!found)
			break;
	}

	// 检查是否所有线程都 finish 了
	for (i = 0; i < NTHREAD; i++) {
		if (!finish[i])
			return 1; // 发现死锁：存在线程无法完成
	}
	return 0;
}

int sys_mutex_create(int blocking)
{
	struct mutex *m = mutex_create(blocking);
	if (m == NULL) {
		errorf("fail to create mutex: out of resource");
		return -1;
	}
	// LAB5: (4-1) You may want to maintain some variables for detect here
	int mutex_id = m - curr_proc()->mutex_pool;
	debugf("create mutex %d", mutex_id);
	return mutex_id;
}

int sys_mutex_lock(int mutex_id)
{
	if (mutex_id < 0 || mutex_id >= curr_proc()->next_mutex_id) {
		errorf("Unexpected mutex id %d", mutex_id);
		return -1;
	}
	// LAB5: (4-1) You may want to maintain some variables for detect
	//       or call your detect algorithm here
	struct proc *p = curr_proc();
	int tid = curr_thread()->tid;
	if (p->deadlock_detect_enable) {
		// 尝试申请资源，设置 Request
		p->mutex_request[tid][mutex_id] = 1;
		// 检测是否会导致死锁
		if (deadlock_detect(p)) {
			// 若死锁，撤销 Request 并返回错误
			p->mutex_request[tid][mutex_id] = 0;
			return -0xDEAD;
		}
		// 若安全，清除 Request (sync.c 中会再次设置，这里清除是为了保持一致性)
		p->mutex_request[tid][mutex_id] = 0;
	}
	mutex_lock(&curr_proc()->mutex_pool[mutex_id]);
	return 0;
}

int sys_mutex_unlock(int mutex_id)
{
	if (mutex_id < 0 || mutex_id >= curr_proc()->next_mutex_id) {
		errorf("Unexpected mutex id %d", mutex_id);
		return -1;
	}
	// LAB5: (4-1) You may want to maintain some variables for detect here
	mutex_unlock(&curr_proc()->mutex_pool[mutex_id]);
	return 0;
}

int sys_semaphore_create(int res_count)
{
	struct semaphore *s = semaphore_create(res_count);
	if (s == NULL) {
		errorf("fail to create semaphore: out of resource");
		return -1;
	}
	// LAB5: (4-2) You may want to maintain some variables for detect here
	int sem_id = s - curr_proc()->semaphore_pool;
	debugf("create semaphore %d", sem_id);
	return sem_id;
}

int sys_semaphore_up(int semaphore_id)
{
	if (semaphore_id < 0 ||
	    semaphore_id >= curr_proc()->next_semaphore_id) {
		errorf("Unexpected semaphore id %d", semaphore_id);
		return -1;
	}
	// LAB5: (4-2) You may want to maintain some variables for detect here
	semaphore_up(&curr_proc()->semaphore_pool[semaphore_id]);
	return 0;
}

int sys_semaphore_down(int semaphore_id)
{
	if (semaphore_id < 0 ||
	    semaphore_id >= curr_proc()->next_semaphore_id) {
		errorf("Unexpected semaphore id %d", semaphore_id);
		return -1;
	}
	// LAB5: (4-2) You may want to maintain some variables for detect
	//       or call your detect algorithm here
	struct proc *p = curr_proc();
	int tid = curr_thread()->tid;
	if (p->deadlock_detect_enable) {
		// 尝试申请资源，设置 Request
		p->sem_request[tid][semaphore_id] = 1;
		// 检测是否会导致死锁
		if (deadlock_detect(p)) {
			// 若死锁，撤销 Request 并返回错误
			p->sem_request[tid][semaphore_id] = 0;
			return -0xDEAD;
		}
		p->sem_request[tid][semaphore_id] = 0;
	}
	semaphore_down(&curr_proc()->semaphore_pool[semaphore_id]);
	return 0;
}

int sys_condvar_create()
{
	struct condvar *c = condvar_create();
	if (c == NULL) {
		errorf("fail to create condvar: out of resource");
		return -1;
	}
	int cond_id = c - curr_proc()->condvar_pool;
	debugf("create condvar %d", cond_id);
	return cond_id;
}

int sys_condvar_signal(int cond_id)
{
	if (cond_id < 0 || cond_id >= curr_proc()->next_condvar_id) {
		errorf("Unexpected condvar id %d", cond_id);
		return -1;
	}
	cond_signal(&curr_proc()->condvar_pool[cond_id]);
	return 0;
}

int sys_condvar_wait(int cond_id, int mutex_id)
{
	if (cond_id < 0 || cond_id >= curr_proc()->next_condvar_id) {
		errorf("Unexpected condvar id %d", cond_id);
		return -1;
	}
	if (mutex_id < 0 || mutex_id >= curr_proc()->next_mutex_id) {
		errorf("Unexpected mutex id %d", mutex_id);
		return -1;
	}
	cond_wait(&curr_proc()->condvar_pool[cond_id],
		  &curr_proc()->mutex_pool[mutex_id]);
	return 0;
}

// LAB5: (2) you may need to define function enable_deadlock_detect here
int sys_enable_deadlock_detect(int is_enable)
{
	struct proc *p = curr_proc();
	p->deadlock_detect_enable = is_enable;
	return 0;
}

/* ch3: 系统调用追踪 */
/* ch4: 更新以检查虚存读写权限 */
int sys_trace(int trace_request, uint64 id, uint8 data)
{
	struct proc *p = curr_proc();
	if (trace_request == 0) {
		/* ch4: 读操作 - 检查地址是否用户可见且可读 */
		pte_t *pte = walk(p->pagetable, id, 0);
		if (pte == NULL || (*pte & PTE_V) == 0 || (*pte & PTE_U) == 0 || (*pte & PTE_R) == 0)
			return -1;
		uint8 *addr = (uint8 *)useraddr(p->pagetable, id);
		if (addr == NULL)
			return -1;
		return *addr;
	} else if (trace_request == 1) {
		/* ch4: 写操作 - 检查地址是否用户可见且可写 */
		pte_t *pte = walk(p->pagetable, id, 0);
		if (pte == NULL || (*pte & PTE_V) == 0 || (*pte & PTE_U) == 0 || (*pte & PTE_W) == 0)
			return -1;
		uint8 *addr = (uint8 *)useraddr(p->pagetable, id);
		if (addr == NULL)
			return -1;
		*addr = data;
		return 0;
	} else if (trace_request == 2) {
		/* ch3: 返回指定syscall的调用次数 */
		if (id >= MAX_SYSCALL_NUM)
			return -1;
		return p->syscall_count[id];
	}
	return -1;
}

/* ch4: sys_mmap - 匿名内存映射 */
int sys_mmap(uint64 start, uint64 len, int prot, int flags)
{
	struct proc *p = curr_proc();

	/* ch4: 检查起始地址页对齐 */
	if (!PGALIGNED(start))
		return -1;

	/* ch4: 检查prot有效性：其他位必须为0 */
	if ((prot & ~0x7) != 0)
		return -1;

	/* ch4: 检查prot至少有一个权限(R/W/X) */
	if ((prot & 0x7) == 0)
		return -1;

	/* ch4: len为0时直接返回成功 */
	if (len == 0)
		return 0;

	/* ch4: 将len向上取整到页边界 */
	uint64 npages = (len + PGSIZE - 1) / PGSIZE;

	/* ch4: 检查[start, start+len)是否已被映射 */
	for (uint64 va = start; va < start + npages * PGSIZE; va += PGSIZE) {
		pte_t *pte = walk(p->pagetable, va, 0);
		if (pte != NULL && (*pte & PTE_V) != 0)
			return -1;  /* 已被映射 */
	}

	/* ch4: 将prot转换为PTE标志位
	 * prot bit 0 = read  -> PTE_R (bit 1)
	 * prot bit 1 = write -> PTE_W (bit 2)
	 * prot bit 2 = exec  -> PTE_X (bit 3)
	 */
	int perm = PTE_U;  /* 用户可访问 */
	if (prot & 0x1) perm |= PTE_R;
	if (prot & 0x2) perm |= PTE_W;
	if (prot & 0x4) perm |= PTE_X;

	/* ch4: 逐页映射（kalloc不支持连续分配） */
	for (uint64 i = 0; i < npages; i++) {
		uint64 va = start + i * PGSIZE;
		char *mem = kalloc();
		if (mem == NULL)
			return -1;  /* 内存不足，简化处理不回滚 */
		memset(mem, 0, PGSIZE);
		if (mappages(p->pagetable, va, PGSIZE, (uint64)mem, perm) != 0) {
			kfree(mem);
			return -1;
		}
	}

	return 0;
}

/* ch4: sys_munmap - 取消内存映射 */
int sys_munmap(uint64 start, uint64 len)
{
	struct proc *p = curr_proc();

	/* ch4: 检查起始地址页对齐 */
	if (!PGALIGNED(start))
		return -1;

	/* ch4: len为0时直接返回成功 */
	if (len == 0)
		return 0;

	/* ch4: 将len向上取整到页边界 */
	uint64 npages = (len + PGSIZE - 1) / PGSIZE;

	/* ch4: 检查[start, start+len)是否全部已映射 */
	for (uint64 va = start; va < start + npages * PGSIZE; va += PGSIZE) {
		pte_t *pte = walk(p->pagetable, va, 0);
		if (pte == NULL || (*pte & PTE_V) == 0)
			return -1;  /* 未映射 */
	}

	/* ch4: 取消映射并释放页面 */
	uvmunmap(p->pagetable, start, npages, 1);

	return 0;
}

/* ch5: sys_spawn - 创建新进程并执行程序 */
int sys_spawn(uint64 path_va)
{
	struct proc *p = curr_proc();
	char path[MAX_STR_LEN];
	struct inode *ip;
	struct proc *np;
	int i;

	/* ch5: 从用户空间拷贝路径名 */
	if (copyinstr(p->pagetable, path, path_va, MAX_STR_LEN) < 0)
		return -1;

	/* ch5: 查找可执行文件 */
	if ((ip = namei(path)) == NULL) {
		errorf("spawn: invalid file name %s\n", path);
		return -1;
	}

	/* ch5: 分配新进程 */
	if ((np = allocproc()) == NULL) {
		iput(ip);
		return -1;
	}

	/* ch5: 设置父进程 */
	np->parent = p;

	/* ch5: 初始化标准IO */
	init_stdio(np);

	/* ch5: 复制父进程的文件描述符表(跳过stdio) */
	for (i = 3; i < FD_BUFFER_SIZE; i++) {
		if (p->files[i] != NULL) {
			p->files[i]->ref++;
			np->files[i] = p->files[i];
		}
	}

	/* ch5: 加载可执行文件 */
	bin_loader(ip, np);
	iput(ip);

	/* ch5: 设置命令行参数 */
	char *argv[2];
	argv[0] = path;
	argv[1] = NULL;
	struct thread *nt = &np->threads[0];
	nt->trapframe->a0 = push_argv(np, argv);

	/* ch5: 将新进程加入调度队列 */
	nt->state = RUNNABLE;
	add_task(nt);

	return np->pid;
}

/* ch5: sys_set_priority - 设置进程优先级 */
int sys_set_priority(long long prio)
{
	/* ch5: 优先级必须 >= 2 */
	if (prio < 2)
		return -1;

	struct proc *p = curr_proc();
	p->priority = prio;
	return prio;
}

/* ch6: Stat结构体定义 */
#define S_IFDIR 0x040000   /* directory */
#define S_IFREG 0x100000   /* regular file */

struct Stat {
	uint64 dev;     /* 设备号 */
	uint64 ino;     /* inode编号 */
	uint32 mode;    /* 文件类型 */
	uint32 nlink;   /* 硬链接数量 */
	uint64 pad[7];  /* 填充 */
};

/* ch6: sys_linkat - 创建硬链接 */
int sys_linkat(int olddirfd, uint64 oldpath_va, int newdirfd, uint64 newpath_va, uint flags)
{
	struct proc *p = curr_proc();
	char oldpath[MAX_STR_LEN], newpath[MAX_STR_LEN];
	struct inode *ip, *dp;

	/* ch6: 从用户空间拷贝路径 */
	if (copyinstr(p->pagetable, oldpath, oldpath_va, MAX_STR_LEN) < 0)
		return -1;
	if (copyinstr(p->pagetable, newpath, newpath_va, MAX_STR_LEN) < 0)
		return -1;

	/* ch6: 新旧路径相同则返回错误 */
	if (strncmp(oldpath, newpath, MAX_STR_LEN) == 0)
		return -1;

	/* ch6: 查找源文件 */
	if ((ip = namei(oldpath)) == NULL)
		return -1;
	ivalid(ip);

	/* ch6: 不能对目录创建硬链接 */
	if (ip->type == T_DIR) {
		iput(ip);
		return -1;
	}

	/* ch6: 增加链接计数 */
	ip->nlink++;
	iupdate(ip);

	/* ch6: 在根目录中添加新的目录项 */
	dp = root_dir();
	if (dirlink(dp, newpath, ip->inum) < 0) {
		/* ch6: 添加失败，恢复链接计数 */
		ip->nlink--;
		iupdate(ip);
		iput(ip);
		iput(dp);
		return -1;
	}

	iput(dp);
	iput(ip);
	return 0;
}

/* ch6: sys_unlinkat - 删除硬链接 */
int sys_unlinkat(int dirfd, uint64 path_va, uint flags)
{
	struct proc *p = curr_proc();
	char path[MAX_STR_LEN];
	struct inode *ip, *dp;

	/* ch6: 从用户空间拷贝路径 */
	if (copyinstr(p->pagetable, path, path_va, MAX_STR_LEN) < 0)
		return -1;

	/* ch6: 查找文件 */
	if ((ip = namei(path)) == NULL)
		return -1;
	ivalid(ip);

	/* ch6: 不能删除目录（简化实现） */
	if (ip->type == T_DIR) {
		iput(ip);
		return -1;
	}

	/* ch6: 从根目录中删除目录项 */
	dp = root_dir();
	if (dirunlink(dp, path) < 0) {
		iput(ip);
		iput(dp);
		return -1;
	}
	iput(dp);

	/* ch6: 减少链接计数 */
	ip->nlink--;
	iupdate(ip);
	iput(ip);  /* ch6: 如果nlink为0，iput会释放inode */

	return 0;
}

/* ch6: sys_fstat - 获取文件状态 */
int sys_fstat(int fd, uint64 st_va)
{
	struct proc *p = curr_proc();
	struct Stat st;

	/* ch6: 检查fd有效性 */
	if (fd < 0 || fd >= FD_BUFFER_SIZE)
		return -1;

	struct file *f = p->files[fd];
	if (f == NULL)
		return -1;

	/* ch6: 只支持inode类型的文件 */
	if (f->type != FD_INODE)
		return -1;

	struct inode *ip = f->ip;
	ivalid(ip);

	/* ch6: 填充Stat结构体 */
	memset(&st, 0, sizeof(st));
	st.dev = 0;  /* 设备号写死为0 */
	st.ino = ip->inum;
	st.mode = (ip->type == T_DIR) ? S_IFDIR : S_IFREG;
	st.nlink = ip->nlink;

	/* ch6: 拷贝到用户空间 */
	if (copyout(p->pagetable, st_va, (char *)&st, sizeof(st)) < 0)
		return -1;

	return 0;
}

/* ch10: sys_npc_register - NPC注册 */
int sys_npc_register(int npc_id)
{
	return ai_sched_register(npc_id);
}

/* ch10: sys_npc_get_status - 获取NPC状态 */
int sys_npc_get_status(uint64 st_va)
{
	struct proc *p = curr_proc();
	struct npc_status st;

	if (ai_sched_get_status(&st) < 0)
		return -1;

	/* ch10: 拷贝到用户空间 */
	if (copyout(p->pagetable, st_va, (char *)&st, sizeof(st)) < 0)
		return -1;

	return 0;
}

/* ch10: sys_npc_yield - NPC让出CPU并触发调度tick */
int sys_npc_yield(void)
{
	/* ch10: 触发进化调度tick */
	ai_sched_tick();
	/* ch10: 让出CPU */
	yield();
	return 0;
}

/* ch11: sys_npc_ipc_notify - 通知内核发生了NPC间通信 */
int sys_npc_ipc_notify(int target_pid, int bytes)
{
	/* ch11: 调用ai_sched_add_ipc累加目标进程的IPC流量 */
	ai_sched_add_ipc(target_pid, bytes);
	return 0;
}

extern char trap_page[];

void syscall()
{
	struct trapframe *trapframe = curr_thread()->trapframe;
	int id = trapframe->a7, ret;
	uint64 args[6] = { trapframe->a0, trapframe->a1, trapframe->a2,
			   trapframe->a3, trapframe->a4, trapframe->a5 };
	/* ch3: 在处理前统计系统调用次数 */
	struct proc *p = curr_proc();
	if (id >= 0 && id < MAX_SYSCALL_NUM) {
		p->syscall_count[id]++;
	}
	if (id != SYS_write && id != SYS_read && id != SYS_sched_yield) {
		debugf("syscall %d args = [%x, %x, %x, %x, %x, %x]", id,
		       args[0], args[1], args[2], args[3], args[4], args[5]);
	}
	switch (id) {
	case SYS_write:
		ret = sys_write(args[0], args[1], args[2]);
		break;
	case SYS_read:
		ret = sys_read(args[0], args[1], args[2]);
		break;
	case SYS_openat:
		ret = sys_openat(args[0], args[1], args[2]);
		break;
	case SYS_close:
		ret = sys_close(args[0]);
		break;
	case SYS_exit:
		sys_exit(args[0]);
		// __builtin_unreachable();
	// case SYS_nanosleep:
	// 	ret = sys_nanosleep(args[0]);
	// 	break;
	case SYS_sched_yield:
		ret = sys_sched_yield();
		break;
	case SYS_gettimeofday:
		ret = sys_gettimeofday(args[0], args[1]);
		break;
	case SYS_getpid:
		ret = sys_getpid();
		break;
	case SYS_getppid:
		ret = sys_getppid();
		break;
	case SYS_clone: // SYS_fork
		ret = sys_clone();
		break;
	case SYS_execve:
		ret = sys_exec(args[0], args[1]);
		break;
	case SYS_wait4:
		ret = sys_wait(args[0], args[1]);
		break;
	case SYS_pipe2:
		ret = sys_pipe(args[0]);
		break;
	case SYS_thread_create:
		ret = sys_thread_create(args[0], args[1]);
		break;
	case SYS_gettid:
		ret = sys_gettid();
		break;
	case SYS_waittid:
		ret = sys_waittid(args[0]);
		break;
	case SYS_mutex_create:
		ret = sys_mutex_create(args[0]);
		break;
	case SYS_mutex_lock:
		ret = sys_mutex_lock(args[0]);
		break;
	case SYS_mutex_unlock:
		ret = sys_mutex_unlock(args[0]);
		break;
	case SYS_semaphore_create:
		ret = sys_semaphore_create(args[0]);
		break;
	case SYS_semaphore_up:
		ret = sys_semaphore_up(args[0]);
		break;
	case SYS_semaphore_down:
		ret = sys_semaphore_down(args[0]);
		break;
	case SYS_condvar_create:
		ret = sys_condvar_create();
		break;
	case SYS_condvar_signal:
		ret = sys_condvar_signal(args[0]);
		break;
	case SYS_condvar_wait:
		ret = sys_condvar_wait(args[0], args[1]);
		break;
	// LAB5: (2) you may need to add case SYS_enable_deadlock_detect here
	case SYS_enable_deadlock_detect:
		ret = sys_enable_deadlock_detect(args[0]);
		break;
	/* ch3: 系统调用追踪 */
	case SYS_trace:
		ret = sys_trace(args[0], args[1], args[2]);
		break;
	/* ch4: 内存映射系统调用 */
	case SYS_mmap:
		ret = sys_mmap(args[0], args[1], args[2], args[3]);
		break;
	case SYS_munmap:
		ret = sys_munmap(args[0], args[1]);
		break;
	/* ch5: spawn系统调用 */
	case SYS_spawn:
		ret = sys_spawn(args[0]);
		break;
	/* ch5: 设置优先级系统调用 */
	case SYS_setpriority:
		ret = sys_set_priority(args[0]);
		break;
	/* ch6: 硬链接相关系统调用 */
	case SYS_linkat:
		ret = sys_linkat(args[0], args[1], args[2], args[3], args[4]);
		break;
	case SYS_unlinkat:
		ret = sys_unlinkat(args[0], args[1], args[2]);
		break;
	case SYS_fstat:
		ret = sys_fstat(args[0], args[1]);
		break;
	/* ch10: 进化调度系统调用 */
	case SYS_npc_register:
		ret = sys_npc_register(args[0]);
		break;
	case SYS_npc_get_status:
		ret = sys_npc_get_status(args[0]);
		break;
	case SYS_npc_yield:
		ret = sys_npc_yield();
		break;
	/* ch11: IPC通知系统调用 */
	case SYS_npc_ipc_notify:
		ret = sys_npc_ipc_notify(args[0], args[1]);
		break;
	default:
		ret = -1;
		errorf("unknown syscall %d", id);
	}
	curr_thread()->trapframe->a0 = ret;
	if (id != SYS_write && id != SYS_read && id != SYS_sched_yield) {
		debugf("syscall %d ret %d", id, ret);
	}
}

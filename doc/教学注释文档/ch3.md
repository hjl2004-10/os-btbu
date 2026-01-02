# lab3

## 第三章：多道程序与分时多任务

## 1. load_app函数功能说明

**作用**：将第 n 个用户应用程序加载到指定的内存地址
### 函数定义

```c
int load_app(int n, uint64 *info)
{
	uint64 start = info[n], end = info[n + 1], length = end - start;//从 'info' 数组中获取第 n 个应用的起始地址 'start' 和结束地址 'end',计算应用程序长度：'length = end - start'
	memset((void *)BASE_ADDRESS + n * MAX_APP_SIZE, 0, MAX_APP_SIZE);//使用 'memset' 将目标内存区域清零（大小为 MAX_APP_SIZE = 128KB）,目标地址：'BASE_ADDRESS + n * MAX_APP_SIZE' (0x80400000 + n * 0x20000)
	memmove((void *)BASE_ADDRESS + n * MAX_APP_SIZE, (void *)start, length);//使用 'memmove' 将应用程序的二进制代码从链接时的位置复制到目标地址使用 'memmove' 将应用程序的二进制代码从链接时的位置复制到目标地址
	return length;//返回应用程序的实际长度（字节数）
}
```
**内存布局**：
- 每个应用占用固定大小的内存空间（128KB）
- 应用 0：[0x80400000, 0x80420000)
- 应用 1：[0x80420000, 0x80440000)
- 应用 n：[0x80400000 + n*0x20000, 0x80400000 + (n+1)*0x20000)

---

## 2. run_all_app函数功能说明

### 函数定义

```c
int run_all_app()
{
	for (int i = 0; i < app_num; ++i) {//循环处理从 0 到 `app_num-1` 的所有应用
		struct proc *p = allocproc();//调用 `allocproc()` 分配进程控制块（PCB）
		struct trapframe *trapframe = p->trapframe;//获取进程的 trapframe
		load_app(i, app_info_ptr);//将第 i 个应用加载到内存
		uint64 entry = BASE_ADDRESS + i * MAX_APP_SIZE;//设置程序计数器，指向应用的入口点
		tracef("load app %d at %p", i, entry);
		trapframe->epc = entry;
		trapframe->sp = (uint64)p->ustack + USER_STACK_SIZE;//设置栈指针，指向用户栈的顶部
		p->state = RUNNABLE;//标记进程为RUNNABLE状态，等待调度器调度
	}
	return 0;
}
```
## 3. proc_init函数功能说明(os/proc.c:26)

### 函数定义
**作用**：在系统启动时初始化进程表（process table）
```c
void proc_init(void)
{
	struct proc *p;
	for (p = pool; p < &pool[NPROC]; p++) {//遍历进程池数组
		p->state = UNUSED;//标记进程槽位为未使用状态
		p->kstack = (uint64)kstack[p - pool];//分配独立的内核栈，每个进程一个页面
		p->ustack = (uint64)ustack[p - pool];//分配用户栈，每个进程一个页面
		p->trapframe = (struct trapframe *)trapframe[p - pool];
		//这行代码尤为重要
                //trapframe是一个二维字符数组：[NPROC][PAGE_SIZE]
                //通过 (struct trapframe *) 强制类型转换，将这块内存重新解释为 struct trapframe *
                //trapframe[p - pool] 获取第 (p - pool) 个内存块
	}
	idle.kstack = (uint64)boot_stack_top;//使用启动时的栈作为 idle 进程的内核栈
	idle.pid = 0;//idle 进程的 PID 为 0
	current_proc = &idle;//将当前进程设置为 idle 进程
}
```

## 4.scheduler函数功能说明
**作用**：进程调度器，负责选择下一个要运行的进程并执行上下文切换。

## 多道程序与协作式调度
### 📋 函数源码

```c

void scheduler(void)
{
    struct proc *p;
    for (;;) {                           // 外层无限循环，调度器永不返回
        for (p = pool; p < &pool[NPROC]; p++) {  // 遍历进程池
            if (p->state == RUNNABLE) {          // 找到可运行的进程
                p->state = RUNNING;              // 将进程状态设为运行中
                current_proc = p;                // 设置为当前进程
                swtch(&idle.context, &p->context); // 切换到该进程
            }
        }
    }
}
```







## 5. set_timer函数功能说明 (os/sbi.c:43)
**作用**：设置定时器中断触发时间

### 函数定义

```c
void set_timer(uint64 stime)  // 函数名：设置定时器；参数stime：定时器触发的绝对时间戳（以CPU时钟周期为单位）
{
	sbi_call(SBI_SET_TIMER, stime, 0, 0);  // 调用SBI接口函数，传入SBI_SET_TIMER命令码和时间参数stime，通过ecall陷入M模式设置mtimecmp寄存器
}
```

**工作原理**：
1. 这是一个SBI（Supervisor Binary Interface）调用的封装函数
2. 通过SBI接口向M模式（机器模式）请求设置定时器
3. 当系统时间达到stime指定的值时，会触发一个定时器中断
4. 调用层次：S模式 -> M模式（通过ecall指令）

---

## 6. set_next_timer函数功能说明 (os/timer.c:18)
**作用**：设置下一次定时器中断的时间

### 函数定义

```c
void set_next_timer()  // 函数名：设置下一次定时器中断的时间
{
	const uint64 timebase = CPU_FREQ / TICKS_PER_SEC;  // 计算时间基数（每次中断的间隔），CPU_FREQ（12500000）除以TICKS_PER_SEC（100）得到125000个时钟周期，相当于10ms
	set_timer(get_cycle() + timebase);  // 设置定时器：获取当前时间（get_cycle读取time寄存器）加上时间间隔timebase，将结果传给set_timer函数设置下次中断时间
}
```

**工作流程**：
1. 计算时间基数 timebase：
   - CPU_FREQ = 12500000 (QEMU中的CPU频率，12.5MHz)
   - TICKS_PER_SEC = 100 (每秒触发100次中断，即10ms一次)
   - timebase = 12500000 / 100 = 125000 个时钟周期

2. 获取当前时间：
   - 调用 get_cycle() 读取当前的时间戳（通过读取RISC-V的time寄存器）

3. 设置下一次中断时间：
   - 下次中断时间 = 当前时间 + timebase
   - 即在当前时间的125000个时钟周期后触发中断（约10ms后）

4. 调用 set_timer() 将计算好的时间设置到定时器

**使用场景**：
- 初始化时调用，启动第一次定时器中断
- 在定时器中断处理函数中调用，实现周期性中断
- 用于操作系统的时间片轮转调度

---

## 7. 完整的 sbi.c 文件带注释

```c
#include "sbi.h"  // 包含SBI接口的头文件声明
#include "types.h"  // 包含类型定义（如uint64）
const uint64 SBI_SET_TIMER = 0;  // SBI调用号：设置定时器
const uint64 SBI_CONSOLE_PUTCHAR = 1;  // SBI调用号：控制台输出字符
const uint64 SBI_CONSOLE_GETCHAR = 2;  // SBI调用号：控制台读取字符
const uint64 SBI_CLEAR_IPI = 3;  // SBI调用号：清除处理器间中断
const uint64 SBI_SEND_IPI = 4;  // SBI调用号：发送处理器间中断
const uint64 SBI_REMOTE_FENCE_I = 5;  // SBI调用号：远程指令缓存刷新
const uint64 SBI_REMOTE_SFENCE_VMA = 6;  // SBI调用号：远程TLB刷新
const uint64 SBI_REMOTE_SFENCE_VMA_ASID = 7;  // SBI调用号：带ASID的远程TLB刷新
const uint64 SBI_SHUTDOWN = 8;  // SBI调用号：系统关机

int inline sbi_call(uint64 which, uint64 arg0, uint64 arg1, uint64 arg2)  // SBI调用的底层函数，which是调用号，arg0-arg2是三个参数
{
	register uint64 a0 asm("a0") = arg0;  // 将第一个参数放入a0寄存器（RISC-V调用约定）
	register uint64 a1 asm("a1") = arg1;  // 将第二个参数放入a1寄存器
	register uint64 a2 asm("a2") = arg2;  // 将第三个参数放入a2寄存器
	register uint64 a7 asm("a7") = which;  // 将SBI调用号放入a7寄存器（SBI规范要求）
	asm volatile("ecall"  // 执行ecall指令，从S模式陷入M模式
		     : "=r"(a0)  // 输出约束：a0寄存器会被修改（存放返回值）
		     : "r"(a0), "r"(a1), "r"(a2), "r"(a7)  // 输入约束：使用a0, a1, a2, a7寄存器
		     : "memory");  // 内存屏障：告诉编译器这条指令可能修改内存
	return a0;  // 返回a0寄存器的值（M模式SBI调用的返回值）
}

void console_putchar(int c)  // 向控制台输出一个字符，参数c是要输出的字符
{
	sbi_call(SBI_CONSOLE_PUTCHAR, c, 0, 0);  // 调用SBI的控制台输出功能，将字符c发送到控制台
}

int console_getchar()  // 从控制台读取一个字符
{
	return sbi_call(SBI_CONSOLE_GETCHAR, 0, 0, 0);  // 调用SBI的控制台输入功能，返回读取到的字符
}

void shutdown()  // 关闭系统
{
	sbi_call(SBI_SHUTDOWN, 0, 0, 0);  // 调用SBI的关机功能，使QEMU虚拟机退出
}

void set_timer(uint64 stime)  // 设置定时器中断触发时间，参数stime是绝对时间戳
{
	sbi_call(SBI_SET_TIMER, stime, 0, 0);  // 调用SBI设置定时器，M模式会将stime写入mtimecmp寄存器，当mtime>=mtimecmp时触发中断
}
```

---

## 8. 完整的 timer.c 文件带注释

```c
#include "timer.h"  // 包含定时器相关的头文件
#include "riscv.h"  // 包含RISC-V架构相关的定义（如CSR寄存器操作）
#include "sbi.h"  // 包含SBI接口函数声明

/// read the `mtime` regiser  // 函数说明：读取mtime寄存器
uint64 get_cycle()  // 获取当前的CPU时钟周期计数
{
	return r_time();  // 调用r_time()函数读取RISC-V的time CSR寄存器，返回当前时间戳
}

/// Enable timer interrupt  // 函数说明：使能定时器中断
void timer_init()  // 初始化定时器
{
	// Enable supervisor timer interrupt  // 注释：使能S模式的定时器中断
	w_sie(r_sie() | SIE_STIE);  // 读取sie寄存器（r_sie），与SIE_STIE位进行或运算，再写回sie寄存器（w_sie），打开定时器中断使能位
	set_next_timer();  // 调用set_next_timer函数，设置第一次定时器中断的触发时间
}

/// Set the next timer interrupt  // 函数说明：设置下一次定时器中断
void set_next_timer()  // 设置下一次定时器中断的触发时间
{
	const uint64 timebase = CPU_FREQ / TICKS_PER_SEC;  // 计算时间基数：CPU频率（12500000Hz）除以每秒中断次数（100次），得到每次中断间隔的时钟周期数（125000），相当于10毫秒
	set_timer(get_cycle() + timebase);  // 调用set_timer函数，参数为当前时间（get_cycle()）加上时间间隔（timebase），设置下次中断在当前时间后10ms触发
}
```

---

## 9. usertrap函数功能说明 (os/trap.c:44)
**作用**：处理来自用户空间的陷阱、异常和系统调用，是操作系统中断处理的核心函数

### 函数定义

```c
void usertrap()  // 函数名：用户态陷阱处理；无参数，无返回值
{
	set_kerneltrap();  // 设置内核陷阱处理向量，确保在内核中发生陷阱时能正确处理
	struct trapframe *trapframe = curr_proc()->trapframe;  // 获取当前进程的trapframe指针，trapframe保存用户态寄存器状态

	if ((r_sstatus() & SSTATUS_SPP) != 0)  // 检查sstatus寄存器的SPP位，如果为1说明来自内核态
		panic("usertrap: not from user mode");  // 抛出异常：usertrap函数只能处理来自用户态的陷阱

	uint64 cause = r_scause();  // 读取scause寄存器，获取导致陷阱的原因（异常类型或中断类型）
	if (cause & (1ULL << 63)) {  // 检查cause的最高位，如果为1表示这是一个中断
		cause &= ~(1ULL << 63);  // 清除最高位，获取中断的实际类型编号

```

```c
		switch (cause) {
		case SupervisorTimer:  // 如果是S模式定时器中断（编号为5）
			tracef("time interrupt!\n");  // 输出调试信息，表示发生了定时器中断
			set_next_timer();  // 设置下一次定时器中断的时间
			yield();  // 放弃当前CPU时间片，触发进程调度
			break;
```
主要是这一段
```c


		default:  // 对于未知的中断类型
			unknown_trap();  // 调用未知陷阱处理函数，通常会导致程序终止
			break;
		}
	} else {  // 如果cause的最高位为0，表示这是一个异常
		switch (cause) {
		case UserEnvCall:  // 用户态环境调用（系统调用）
			trapframe->epc += 4;  // 将程序计数器epc增加4，跳过ecall指令，从下一条指令继续执行
			syscall();  // 调用系统调用处理函数，根据寄存器中的参数执行相应的内核服务
			break;
		case StoreMisaligned:  // 存储地址未对齐异常
		case StorePageFault:   // 存储页错误异常
		case InstructionMisaligned:  // 指令地址未对齐异常
		case InstructionPageFault:   // 指令页错误异常
		case LoadMisaligned:   // 加载地址未对齐异常
		case LoadPageFault:    // 加载页错误异常
			printf("%d in application, bad addr = %p, bad instruction = %p, "
			       "core dumped.\n",
			       cause, r_stval(), trapframe->epc);  // 输出错误信息，包括异常类型、错误地址和指令地址
			exit(-2);  // 退出进程，返回码-2表示内存相关错误
			break;
		case IllegalInstruction:  // 非法指令异常
			printf("IllegalInstruction in application, core dumped.\n");  // 输出非法指令错误信息
			exit(-3);  // 退出进程，返回码-3表示非法指令错误
			break;
		default:  // 对于未知的异常类型
			unknown_trap();  // 调用未知陷阱处理函数
			break;
		}
	}
	usertrapret();  // 准备返回用户态，设置必要的寄存器状态和页表
}
```


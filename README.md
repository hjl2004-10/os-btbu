# os-btbu
操作系统
队伍名称：灵汐队
学校：北京工商大学
指导教师:赵霞
开发人员：何俊林，许智辰
文档：狄昊然
12月  2025

## 项目结构

```
os-btbu/
├── bootloader/              # 引导加载程序
├── os/                      # 内核源码
│   ├── main.c               # 内核入口
│   ├── proc.c/h             # 进程管理
│   ├── vm.c/h               # 虚拟内存管理
│   ├── syscall.c/h          # 系统调用处理
│   ├── trap.c/h             # 中断/异常处理
│   ├── sync.c/h             # 同步原语(信号量/互斥锁/条件变量)
│   ├── ai_sched.c/h         # NPC进化调度(ch10)
│   ├── npc_memory.c/h       # NPC三级记忆系统(ch11)
│   ├── file.c/h             # 文件系统
│   ├── pipe.c               # 管道实现
│   ├── fs.c/h               # 文件系统操作
│   ├── kalloc.c/h           # 内核内存分配
│   ├── plic.c/h             # 中断控制器
│   ├── console.c/h          # 控制台驱动
│   ├── virtio_disk.c        # VirtIO磁盘驱动
│   ├── loader.c/h           # 程序加载器
│   ├── entry.S              # 内核入口汇编
│   ├── trampoline.S         # 用户态/内核态切换
│   └── kernel.ld            # 链接脚本
├── net/                     # 网络协议栈(ch9)
│   ├── ai_api.c/h           # AI API客户端
│   ├── http.c/h             # HTTP客户端
│   ├── tcp.c/h              # TCP协议
│   ├── udp.c/h              # UDP协议
│   ├── ip.c/h               # IP协议
│   ├── icmp.c/h             # ICMP协议
│   ├── arp.c/h              # ARP协议
│   ├── ether.c/h            # 以太网帧处理
│   ├── virtio_net.c         # VirtIO网卡驱动
│   └── net.c/h              # 网络层统一接口
├── user/                    # 用户态程序
│   ├── src/                 # 用户程序源码
│   │   ├── ch2b_*.c         # ch2: 基础程序
│   │   ├── ch3_*.c          # ch3: 系统调用追踪
│   │   ├── ch4_*.c          # ch4: 内存映射
│   │   ├── ch5_*.c          # ch5: 进程管理
│   │   ├── ch6_*.c          # ch6: 文件系统
│   │   ├── ch7b_*.c         # ch7: 管道通信
│   │   ├── ch8_*.c          # ch8: 并发同步
│   │   ├── ch10_world.c     # ch10: 进化调度-世界管理器
│   │   ├── ch10_npc.c       # ch10: 进化调度-NPC进程
│   │   ├── ch11_world.c     # ch11: NPC社交-世界管理器
│   │   ├── ch11_npc.c       # ch11: NPC社交-四线程NPC
│   │   └── usershell.c      # 用户shell
│   └── lib/                 # 用户态库
│       ├── syscall.c/h      # 系统调用封装
│       ├── ai_sched.c       # NPC调度用户接口
│       └── arch/            # 架构相关代码
├── doc/                     # 文档目录
│   ├── 【Main】教学文档/    # 教学指导书(md+pdf)
│   ├── 代码调试文档/        # 调试记录
│   └── 教学注释文档/        # 代码注释说明
├── scripts/                 # 构建脚本
├── nfs/                     # NFS文件系统
├── Makefile                 # 构建文件
└── README.md                # 项目说明
```

## 更新日志

### 2025-11-25
- 导入清华ucore（rcore的c语言版）ch8内容作为基底，并打算补充前面章节的程序补充
- 1.完成清华实验
- 2.完成北工商版实验指导书
- 3.按照AI-OS的新构思，开发了ch9-ch11三个新实验
  - 3.1 提出新目标和新概念：
    - **AI-OS理念**：让操作系统具备调用AI能力，为智能应用提供底层支持
    - **NPC生态系统**：将进程抽象为具有"生命周期"的NPC，通过资源竞争实现自然选择
    - **三级记忆模型**：L1人设(硬编码) + L2记忆(内核存储) + L3会话(进程内存)
  - 3.2 设计新任务：
    - **ch9 网络协议栈**：实现完整TCP/IP协议栈，支持从内核调用外部AI API
    - **ch10 进化调度**：实现NPC沙盒机制，活力衰减+OOM淘汰实现"自然选择"
    - **ch11 NPC社交**：四线程架构(感知/思考/沟通/记忆) + 管道通信 + AI驱动决策
  - 3.3 实现新功能：
    - 以太网/ARP/IP/ICMP/UDP/TCP协议栈 + VirtIO网卡驱动
    - HTTP客户端 + AI API调用接口
    - 新增系统调用：npc_register、npc_get_status、npc_yield、npc_ipc_notify、npc_ai_chat、npc_memory_save、npc_memory_load
  - 3.4 创新点：
    - **首次在uCore教学OS中实现完整网络协议栈并成功调用外部AI**
    - **提出"进程即NPC"的生态模型，用操作系统机制模拟生命演化**
    - **设计用户态多线程+内核AI调用的混合架构**
### 2025-11-30
- 实现 ch3 sys_trace 系统调用（读内存、写内存、统计syscall调用次数）
- 实现 ch4 mmap/munmap 系统调用（匿名内存映射）
- 修复 sys_trace 虚存权限检查（读操作检查PTE_R，写操作检查PTE_W）
- 修复死锁检测算法的竞态条件问题

### 2025-12-13
- 实现 ch5 sys_spawn 系统调用（创建新进程并执行程序，相当于fork+exec但不复制内存）
- 实现 ch5 sys_set_priority 系统调用（设置进程优先级）
- 实现 stride 调度算法（基于优先级的进程调度，优先级越高获得CPU时间越多）
- 通过 ch5b_usertest 和 ch5t_usertest 全部测试
- 实现 ch6 sys_linkat 系统调用（创建硬链接）
- 实现 ch6 sys_unlinkat 系统调用（删除硬链接）
- 实现 ch6 sys_fstat 系统调用（获取文件状态信息）
- 为 inode 添加 nlink 字段支持硬链接计数
- 修复 freewalk 函数以支持 mmap 区域的正确清理
- 通过 ch6b_usertest 和 ch6_usertest 全部测试
- 添加 ch3-ch6 教学文档（doc/目录）
- 添加 ch9 TCP/IP 网络协议栈（移植自 xv6-riscv-net/microps）
  - 支持以太网、ARP、IP、ICMP、UDP、TCP 协议
  - VirtIO 网络设备驱动（QEMU virtio-net-device）
  - 完整的 TCP 状态机实现
  - 为后续实现 HTTP 客户端调用 AI API 做准备

### 2025-12-14
- 集成网络协议栈到内核
  - 修改 Makefile 支持 net/ 目录编译
  - 添加 VIRTIO1 网络设备内存映射和中断处理
  - 在 main.c 中初始化网络协议栈
  - QEMU 启动参数添加 virtio-net-device
- 实现 HTTP 客户端（net/http.c）
  - 支持 HTTP GET/POST 请求
  - URL 解析、请求构建、响应解析
  - 支持 Authorization 头（用于API认证）
- 实现 AI API 客户端（net/ai_api.c）
  - 支持 OpenAI 兼容的 chat/completions 接口
  - JSON 请求构建和响应解析
  - 可配置 host、port、api_key、model
- **成功从内核调用外部 AI API**
  - 测试服务器：119.3.217.132:8000
  - 模型：lingxi-1
  - 收到响应："Hello! How can I assist you today? 😊"
- 实现 ch10 进化调度（NPC沙盒）
  - 新增 3 个系统调用：npc_register(483)、npc_get_status(484)、npc_yield(485)
  - 实现活力衰减机制：每tick优先级-1
  - 实现 OOM Killer：优先级归0时NPC被淘汰
  - 预留 IPC 奖励接口：为 ch11 社交系统做准备
  - 用户态程序：ch10_world（世界管理器）、ch10_npc（NPC进程）
  - 测试验证：3个NPC并发运行约50轮后全部因活力耗尽而生命周期终止

### 2025-12-25
- 实现 ch11 NPC通信系统（阶段一：四线程框架）
  - NPC进程采用四线程架构：感知(perception)、思考(thinking)、沟通(communication)、记忆(memory)
  - 使用现有同步原语：mutex_blocking_create()、condvar_create()
  - 线程间通过共享数据结构 npc_shared 通信
  - 思考线程支持两种触发：收到消息触发、随机主动发起(20%概率)
  - 支持 [to none] 决策：NPC可选择不发送消息
  - 用户态程序：ch11_world（世界管理器）、ch11_npc（NPC四线程进程）
- 实现 ch11 NPC社交系统（阶段二：管道通信）
  - 世界进程创建NPC间管道矩阵 pipes[from][to][2]
  - 通过命令行参数传递管道fd给NPC子进程
  - perception线程真正从管道读取消息
  - communication线程真正通过管道发送消息
  - 新增系统调用 npc_ipc_notify(486)：通知内核IPC流量用于奖励计算
- 修复内核 sys_pipe() bug
  - 修复数据类型不匹配：原代码用sizeof(uint64)写fd，但用户态int[2]期望sizeof(int)
  - 修复未初始化变量检查：原代码比较未初始化指针
- 修复 perception 线程阻塞退出问题
  - 问题：piperead()在写端开着时会阻塞，而fork后其他进程持有写端
  - 解决：不等待perception线程，进程退出时由内核清理
- 设计文档：doc/ch11-npc-social.md、doc/ch11-impl-details.md
- 待实现：阶段三(NPC群聊机制)

### 2025-12-26
- 实现 ch12 NPC人格塑造系统（阶段一：AI驱动决策+三级记忆）
  - 新增系统调用 npc_ai_chat(487)：用户态调用内核AI API
  - 新增系统调用 npc_memory_save(488)：保存NPC的L2记忆到内核
  - 新增系统调用 npc_memory_load(489)：从内核读取NPC的L2记忆
  - 实现三级记忆系统：
    - L1(人设)：硬编码的NPC性格模板（外向热情/内向沉稳/幽默风趣）
    - L2(AI记忆)：内核存储，通过系统调用读写
    - L3(会话历史)：进程内存，记录最近对话
  - thinking线程改为AI驱动：构建prompt(L1+L2+L3+situation)，调用AI获取决策
  - AI响应格式：`[to npcX]: 消息` 和 `[memory]: 记忆内容`
  - 修复AI响应解析器：支持大小写（`[to NPC1]` 和 `[to npc1]` 均可识别）
  - AI调用失败时的回退机制：简单回复"收到!"
- 新增内核模块：os/npc_memory.c、os/npc_memory.h
- 优化 user/Makefile：运行 `make BASE=1 CHAPTER=x` 后自动将 build/bin 复制到 target/bin，无需手动复制
- 测试验证：NPC成功通过AI进行多轮对话
- 待实现（阶段二：情绪的演化）（阶段三：性格的演化）
- 待实现ch13 NPC社交（关系模式构建，同学，师生，亲子等简易模式）系统
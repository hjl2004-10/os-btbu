# os-btbu
操作系统
队伍名称：灵汐队
学校：北京工商大学
开发人员：何俊林，许智辰
文档：狄昊然
12月  2025

## 更新日志

### 2025-11-25
- 导入清华ucore（rcore的c语言版）ch8内容作为基底，并打算补充前面章节的程序补充（ch1和ch2为环境搭建故无内容添加）
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
  - 实现优先级衰减机制：每tick优先级-1
  - 实现 OOM Killer：优先级归0时NPC被淘汰
  - 预留 IPC 奖励接口：为 ch11 社交系统做准备
  - 用户态程序：ch10_world（世界管理器）、ch10_npc（NPC进程）
  - 测试验证：3个NPC并发运行约50轮后全部因优先级耗尽而死亡

### 2025-12-25
- 实现 ch11 NPC社交系统（阶段一：四线程框架）
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
- 待实现：阶段三(AI驱动+三级记忆)

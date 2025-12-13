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


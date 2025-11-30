#ifndef __FS_H__
#define __FS_H__

#include "types.h"
// 磁盘文件系统格式。
// 内核和用户程序都使用此头文件。

#define NFILE 100 // 每个系统打开的文件数
#define NINODE 50 // 最大活动 i-node 数
#define NDEV 10 // 最大主设备号
#define ROOTDEV 1 // 文件系统根磁盘的设备号
#define MAXOPBLOCKS 10 // 任何 FS 操作写入的最大块数
#define NBUF (MAXOPBLOCKS * 3) // 磁盘块缓存大小
#define FSSIZE 1000 // 文件系统大小（以块为单位）
#define MAXPATH 128 // 最大文件路径名

#define ROOTINO 1 // 根 i-number
#define BSIZE 1024 // 块大小

// 磁盘布局：
// [ 引导块 | 超级块 | inode 块 | 空闲位图 | 数据块]
//
// mkfs 计算超级块并构建初始文件系统。
// 超级块描述磁盘布局：
struct superblock {
	uint magic; // 必须是 FSMAGIC
	uint size; // 文件系统镜像大小（块）
	uint nblocks; // 数据块数量
	uint ninodes; // inode 数量
	uint inodestart; // 第一个 inode 块的块号
	uint bmapstart; // 第一个空闲位图块的块号
};

#define FSMAGIC 0x10203040

#define NDIRECT 12
#define NINDIRECT (BSIZE / sizeof(uint))
#define MAXFILE (NDIRECT + NINDIRECT)

// 文件类型
#define T_DIR 1 // 目录
#define T_FILE 2 // 文件

// LAB4: 修改后保持与 os/fs.h 中的 dinode 一致
// 磁盘 inode 结构
struct dinode {
	short type; // 文件类型
	short pad[3];
	uint size; // 文件大小（字节）
	uint addrs[NDIRECT + 1]; // 数据块地址
};

// 每块 inode 数。
#define IPB (BSIZE / sizeof(struct dinode))

// 包含 inode i 的块
#define IBLOCK(i, sb) ((i) / IPB + sb.inodestart)

// 每块位图位数
#define BPB (BSIZE * 8)

// 包含块 b 的位的空闲位图块
#define BBLOCK(b, sb) ((b) / BPB + sb.bmapstart)

// 目录是包含一系列 dirent 结构的文件。
#define DIRSIZ 14

struct dirent {
	ushort inum;
	char name[DIRSIZ];
};

#endif //!__FS_H__

// 缓冲区缓存。
//
// 缓冲区缓存是持有磁盘块内容缓存副本的 buf 结构链表。
// 在内存中缓存磁盘块可以减少磁盘读取次数，
// 并为多个进程使用的磁盘块提供同步点。
//
// 接口:
// * 要获取特定磁盘块的缓冲区，请调用 bread。
// * 修改缓冲区数据后，调用 bwrite 将其写入磁盘。
// * 使用完缓冲区后，调用 brelse。
// * 调用 brelse 后不要再使用缓冲区。
// * 一次只有一个进程可以使用缓冲区，
//     所以不要持有它们超过必要的时间。

#include "bio.h"
#include "defs.h"
#include "fs.h"
#include "riscv.h"
#include "types.h"
#include "virtio.h"

struct {
	struct buf buf[NBUF];
	struct buf head;
} bcache;

void binit()
{
	struct buf *b;
	// 创建缓冲区链表
	bcache.head.prev = &bcache.head;
	bcache.head.next = &bcache.head;
	for (b = bcache.buf; b < bcache.buf + NBUF; b++) {
		b->next = bcache.head.next;
		b->prev = &bcache.head;
		bcache.head.next->prev = b;
		bcache.head.next = b;
	}
}

// 在缓冲区缓存中查找设备 dev 上的块。
// 如果未找到，分配一个缓冲区。
static struct buf *bget(uint dev, uint blockno)
{
	struct buf *b;
	// 块是否已缓存？
	for (b = bcache.head.next; b != &bcache.head; b = b->next) {
		if (b->dev == dev && b->blockno == blockno) {
			b->refcnt++;
			return b;
		}
	}
	// 未缓存。
	// 回收最近最少使用 (LRU) 的未使用缓冲区。
	for (b = bcache.head.prev; b != &bcache.head; b = b->prev) {
		if (b->refcnt == 0) {
			b->dev = dev;
			b->blockno = blockno;
			b->valid = 0;
			b->refcnt = 1;
			return b;
		}
	}
	panic("bget: no buffers");
	return 0;
}

const int R = 0;
const int W = 1;

// 返回包含指定块内容的 buf。
struct buf *bread(uint dev, uint blockno)
{
	struct buf *b;
	b = bget(dev, blockno);
	if (!b->valid) {
		virtio_disk_rw(b, R);
		b->valid = 1;
	}
	return b;
}

// 将 b 的内容写入磁盘。
void bwrite(struct buf *b)
{
	virtio_disk_rw(b, W);
}

// 释放缓冲区。
// 移动到最近最常使用列表的头部。
void brelse(struct buf *b)
{
	b->refcnt--;
	if (b->refcnt == 0) {
		// 没有人在等待它。
		b->next->prev = b->prev;
		b->prev->next = b->next;
		b->next = bcache.head.next;
		b->prev = &bcache.head;
		bcache.head.next->prev = b;
		bcache.head.next = b;
	}
}

void bpin(struct buf *b)
{
	b->refcnt++;
}

void bunpin(struct buf *b)
{
	b->refcnt--;
}

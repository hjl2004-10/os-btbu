/* ch9: 网络协议栈 - 工具函数实现 */
#include "platform.h"
#include "util.h"

/*
 * Queue
 */

struct queue_entry {
    struct queue_entry *next;
    void *data;
};

void
queue_init(struct queue_head *queue)
{
    queue->head = 0;
    queue->tail = 0;
    queue->num = 0;
}

void *
queue_push(struct queue_head *queue, void *data)
{
    struct queue_entry *entry;

    if (!queue) {
        return 0;
    }
    entry = memory_alloc(sizeof(*entry));
    if (!entry) {
        return 0;
    }
    entry->next = 0;
    entry->data = data;
    if (queue->tail) {
        queue->tail->next = entry;
    }
    queue->tail = entry;
    if (!queue->head) {
        queue->head = entry;
    }
    queue->num++;
    return data;
}

void *
queue_pop(struct queue_head *queue)
{
    struct queue_entry *entry;
    void *data;

    if (!queue || !queue->head) {
        return 0;
    }
    entry = queue->head;
    queue->head = entry->next;
    if (!queue->head) {
        queue->tail = 0;
    }
    queue->num--;
    data = entry->data;
    memory_free(entry);
    return data;
}

void *
queue_peek(struct queue_head *queue)
{
    if (!queue || !queue->head) {
        return 0;
    }
    return queue->head->data;
}

void
queue_foreach(struct queue_head *queue, void (*func)(void *arg, void *data), void *arg)
{
    struct queue_entry *entry;

    if (!queue || !func) {
        return;
    }
    for (entry = queue->head; entry; entry = entry->next) {
        func(arg, entry->data);
    }
}

/*
 * Byteorder
 */

#ifndef __BIG_ENDIAN
#define __BIG_ENDIAN 4321
#endif
#ifndef __LITTLE_ENDIAN
#define __LITTLE_ENDIAN 1234
#endif

static int endian;

static int
byteorder(void) {
    uint32 x = 0x00000001;

    return *(uint8 *)&x ? __LITTLE_ENDIAN : __BIG_ENDIAN;
}

static uint16
byteswap16(uint16 v)
{
    return (v & 0x00ff) << 8 | (v & 0xff00 ) >> 8;
}

static uint32
byteswap32(uint32 v)
{
    return (v & 0x000000ff) << 24 | (v & 0x0000ff00) << 8 | (v & 0x00ff0000) >> 8 | (v & 0xff000000) >> 24;
}

uint16
hton16(uint16 h)
{
    if (!endian) {
        endian = byteorder();
    }
    return endian == __LITTLE_ENDIAN ? byteswap16(h) : h;
}

uint16
ntoh16(uint16 n)
{
    if (!endian) {
        endian = byteorder();
    }
    return endian == __LITTLE_ENDIAN ? byteswap16(n) : n;
}

uint32
hton32(uint32 h)
{
    if (!endian) {
        endian = byteorder();
    }
    return endian == __LITTLE_ENDIAN ? byteswap32(h) : h;
}

uint32
ntoh32(uint32 n)
{
    if (!endian) {
        endian = byteorder();
    }
    return endian == __LITTLE_ENDIAN ? byteswap32(n) : n;
}

/*
 * Checksum
 */

uint16
cksum16(uint16 *addr, uint16 count, uint32 init)
{
    uint32 sum;

    sum = init;
    while (count > 1) {
        sum += *(addr++);
        count -= 2;
    }
    if (count > 0) {
        sum += *(uint8 *)addr;
    }
    while (sum >> 16) {
        sum = (sum & 0xffff) + (sum >> 16);
    }
    return ~(uint16)sum;
}

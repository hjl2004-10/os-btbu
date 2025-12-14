/* ch9: 网络协议栈 - 工具函数 */
#ifndef UTIL_H
#define UTIL_H

#include "platform.h"

/*
 * Compare
 */

#ifndef MAX
#define MAX(x, y) ((x) > (y) ? (x) : (y))
#endif
#ifndef MIN
#define MIN(x, y) ((x) < (y) ? (x) : (y))
#endif

/*
 * Array
 */

#define countof(x) ((sizeof(x) / sizeof(*x)))
#define tailof(x) (x + countof(x))
#define indexof(x, y) (((uint64)y - (uint64)x) / sizeof(*y))

/*
 * Time
 */

#define timeval_add_usec(x, y)         \
    do {                               \
        (x)->tv_sec += y / 1000000;    \
        (x)->tv_usec += y % 1000000;   \
        if ((x)->tv_usec >= 1000000) { \
            (x)->tv_sec += 1;          \
            (x)->tv_usec -= 1000000;   \
        }                              \
    } while(0);

/*
 * Logging
 * 注意：如果已经通过platform.h包含了os/log.h，则使用os/log.h的定义
 */

#ifndef errorf
#define errorf(...) printf(__VA_ARGS__), printf("\n")
#endif
#ifndef warnf
#define warnf(...) printf(__VA_ARGS__), printf("\n")
#endif
#ifndef infof
#define infof(...) printf(__VA_ARGS__), printf("\n")
#endif
#ifndef debugf
#ifdef NET_DEBUG
#define debugf(...) printf(__VA_ARGS__), printf("\n")
#else
#define debugf(...)
#endif
#endif
#ifndef debugdump
#define debugdump(...)
#endif

/*
 * Queue
 */

struct queue_entry;

struct queue_head {
    struct queue_entry *head;
    struct queue_entry *tail;
    unsigned int num;
};

extern void
queue_init(struct queue_head *queue);
extern void *
queue_push(struct queue_head *queue, void *data);
extern void *
queue_pop(struct queue_head *queue);
extern void *
queue_peek(struct queue_head *queue);
extern void
queue_foreach(struct queue_head *queue, void (*func)(void *arg, void *data), void *arg);

/*
 * Byteorder
 */

extern uint16
hton16(uint16 h);
extern uint16
ntoh16(uint16 n);
extern uint32
hton32(uint32 h);
extern uint32
ntoh32(uint32 n);

/*
 * Checksum
 */

extern uint16
cksum16(uint16 *addr, uint16 count, uint32 init);

#endif

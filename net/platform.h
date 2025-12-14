/* ch9: 网络协议栈 - 平台适配层 (uCore/os-btbu) */
#ifndef PLATFORM_H
#define PLATFORM_H

#include "../os/types.h"
#include "../os/riscv.h"
#include "../os/const.h"
#include "../os/kalloc.h"
#include "../os/string.h"
#include "../os/printf.h"
#include "../os/log.h"

/*
 * Standard definitions
 */

#define UINT16_MAX 65535

#define isascii(x) ((x >= 0x00) && (x <= 0x7f))
#define isprint(x) ((x >= 0x20) && (x <= 0x7e))

#define EINTR 4

extern int net_errno;

/*
 * Time structures
 */

struct timeval {
    long tv_sec;
    long tv_usec;
};

struct tm {
    int tm_sec;
    int tm_min;
    int tm_hour;
    int tm_mday;
    int tm_mon;
    int tm_year;
    int tm_wday;
    int tm_yday;
    int tm_isdst;
};

struct timespec {
    long tv_sec;
    long tv_nsec;
};

/*
 * Time functions
 */

extern void gettimeofday(struct timeval *tv, void *tz);
extern struct tm *localtime_r(const long *timep, struct tm *result);

static inline void
timersub(struct timeval *a, struct timeval *b, struct timeval *res)
{
    res->tv_sec = a->tv_sec - b->tv_sec;
    res->tv_usec = a->tv_usec - b->tv_usec;
    if (res->tv_usec < 0) {
        --res->tv_sec;
        res->tv_usec += 1000*1000;
    }
}

static inline void
timerclear(struct timeval *tv)
{
    tv->tv_sec = 0;
    tv->tv_usec = 0;
}

static inline void
timeval_add_usec(struct timeval *tv, unsigned int usec)
{
    tv->tv_usec += usec;
    while (tv->tv_usec >= 1000000) {
        tv->tv_sec++;
        tv->tv_usec -= 1000000;
    }
}

#define timercmp(a, b, cmp) \
    ((a)->tv_sec == (b)->tv_sec ? (a)->tv_usec cmp (b)->tv_usec : (a)->tv_sec cmp (b)->tv_sec)

/*
 * String functions
 */

extern long strtol(const char *s, char **endptr, int base);
extern char *strrchr(const char *cp, int ch);

/*
 * Random
 */

extern long random(void);
extern void srand(unsigned int seed);

/*
 * Memory
 */

static inline void *
memory_alloc(uint size)
{
    void *p;

    if (PGSIZE < size) {
        return 0;
    }
    p = kalloc();
    if (p) {
        memset(p, 0, size);
    }
    return p;
}

static inline void
memory_free(void *ptr)
{
    kfree(ptr);
}

/*
 * Simple spinlock using interrupt enable/disable
 * os-btbu不使用spinlock，我们用关中断实现简单互斥
 */

typedef struct {
    int locked;
    int intena;  /* 保存之前的中断状态 */
} mutex_t;

#define MUTEX_INITIALIZER {0, 0}

static inline int
mutex_init(mutex_t *m)
{
    m->locked = 0;
    m->intena = 0;
    return 0;
}

static inline int
mutex_lock(mutex_t *m)
{
    /* 关中断并自旋 */
    int intena = (r_sstatus() & SSTATUS_SIE) != 0;
    intr_off();

    while (__sync_lock_test_and_set(&m->locked, 1) != 0) {
        /* spin */
    }

    m->intena = intena;
    __sync_synchronize();
    return 0;
}

static inline int
mutex_unlock(mutex_t *m)
{
    int intena = m->intena;

    __sync_synchronize();
    __sync_lock_release(&m->locked);

    /* 恢复中断状态 */
    if (intena) {
        intr_on();
    }
    return 0;
}

/*
 * Interrupt handling for network
 */

#define INTR_IRQ_SOFTIRQ 0x01
#define INTR_IRQ_EVENT   0x02

extern int net_pending;
extern mutex_t net_pendinglock;

static inline int
intr_raise_irq(unsigned int irq)
{
    mutex_lock(&net_pendinglock);
    net_pending |= irq;
    mutex_unlock(&net_pendinglock);
    return 0;
}

static inline int
intr_init(void)
{
    return 0;
}

static inline int
intr_run(void)
{
    return 0;
}

static inline void
intr_shutdown(void)
{
    return;
}

/*
 * Scheduler context for blocking operations
 * 在os-btbu中使用简单的忙等待（busy-wait）实现
 */

struct sched_ctx {
    int interrupted;
    int wc; /* wait count */
    int ready; /* 用于简单的等待/唤醒机制 */
};

#define SCHED_CTX_INITIALIZER {0, 0, 0}

static inline int
sched_ctx_init(struct sched_ctx *ctx)
{
    ctx->interrupted = 0;
    ctx->wc = 0;
    ctx->ready = 0;
    return 0;
}

static inline int
sched_ctx_destroy(struct sched_ctx *ctx)
{
    if (ctx->wc) {
        return -1;
    }
    return 0;
}

/* ch9: 网络软中断处理函数声明 */
extern int net_softirq_handler(void);
extern int net_timer_handler(void);

/* 简化的sleep：使用忙等待+轮询网络（适用于内核网络操作） */
static inline int
sched_sleep(struct sched_ctx *ctx, mutex_t *mutex, const struct timespec *abstime)
{
    int timeout_loops = 100000000; /* 超时计数器 */
    (void)abstime;

    if (ctx->interrupted) {
        net_errno = EINTR;
        return -1;
    }

    ctx->wc++;
    ctx->ready = 0;

    /* 释放锁后等待 */
    mutex_unlock(mutex);

    /* 忙等待直到被唤醒或中断，同时处理网络事件 */
    while (!ctx->ready && !ctx->interrupted && timeout_loops > 0) {
        /* ch9: 开中断让网络中断能够到达 */
        intr_on();

        /* 处理网络软中断 */
        if (net_pending & INTR_IRQ_SOFTIRQ) {
            mutex_lock(&net_pendinglock);
            net_pending &= ~INTR_IRQ_SOFTIRQ;
            mutex_unlock(&net_pendinglock);
            net_softirq_handler();
        }

        /* 处理定时器 */
        net_timer_handler();

        __sync_synchronize();
        timeout_loops--;
    }

    intr_off();

    /* 重新获取锁 */
    mutex_lock(mutex);

    ctx->wc--;

    if (ctx->interrupted) {
        if (!ctx->wc) {
            ctx->interrupted = 0;
        }
        net_errno = EINTR;
        return -1;
    }
    return 0;
}

static inline int
sched_wakeup(struct sched_ctx *ctx)
{
    ctx->ready = 1;
    __sync_synchronize();
    return 0;
}

static inline int
sched_interrupt(struct sched_ctx *ctx)
{
    ctx->interrupted = 1;
    __sync_synchronize();
    return 0;
}

/*
 * Platform initialization
 */
void net_platform_init(void);

#endif

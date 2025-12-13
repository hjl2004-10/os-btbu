/* ch9: 网络协议栈 - 平台适配层 (uCore) */
#ifndef PLATFORM_H
#define PLATFORM_H

#include "../types.h"
#include "../riscv.h"
#include "../defs.h"
#include "../param.h"
#include "../spinlock.h"
#include "../proc.h"

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
 * Mutex
 */

typedef struct spinlock mutex_t;

#define MUTEX_INITIALIZER {0}

static inline int
mutex_init(mutex_t *mutex)
{
    initlock(mutex, "net_mutex");
    return 0;
}

static inline int
mutex_lock(mutex_t *mutex)
{
    acquire(mutex);
    return 0;
}

static inline int
mutex_unlock(mutex_t *mutex)
{
    release(mutex);
    return 0;
}

/*
 * Interrupt
 */

#define INTR_IRQ_SOFTIRQ 0x01
#define INTR_IRQ_EVENT   0x02

extern struct spinlock net_pendinglock;
extern int net_pending;

static inline int
intr_raise_irq(unsigned int irq)
{
    acquire(&net_pendinglock);
    net_pending |= irq;
    release(&net_pendinglock);
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
 */

struct sched_ctx {
    int interrupted;
    int wc; /* wait count */
};

#define SCHED_CTX_INITIALIZER {0, 0}

static inline int
sched_ctx_init(struct sched_ctx *ctx)
{
    ctx->interrupted = 0;
    ctx->wc = 0;
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

static inline int
sched_sleep(struct sched_ctx *ctx, mutex_t *mutex, const struct timespec *abstime)
{
    (void)abstime;
    if (ctx->interrupted) {
        net_errno = EINTR;
        return -1;
    }
    ctx->wc++;
    sleep(ctx, mutex);
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
    wakeup(ctx);
    return 0;
}

static inline int
sched_interrupt(struct sched_ctx *ctx)
{
    ctx->interrupted = 1;
    wakeup(ctx);
    return 0;
}

#endif

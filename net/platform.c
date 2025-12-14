/* ch9: 网络协议栈 - 平台适配实现 (uCore/os-btbu) */
#include "platform.h"

/* 全局错误号 */
int net_errno = 0;

/* 网络中断待处理标志 */
int net_pending = 0;
mutex_t net_pendinglock = MUTEX_INITIALIZER;

/* 随机数生成器状态 */
static unsigned int rand_seed = 1;

/*
 * Time functions
 */

void
gettimeofday(struct timeval *tv, void *tz)
{
    (void)tz;
    uint64 ticks = r_time();
    /* 假设时钟频率为10MHz (QEMU默认) */
    tv->tv_sec = ticks / 10000000;
    tv->tv_usec = (ticks % 10000000) / 10;
}

struct tm *
localtime_r(const long *timep, struct tm *result)
{
    long t = *timep;
    /* 简化实现：只计算时分秒 */
    result->tm_sec = t % 60;
    result->tm_min = (t / 60) % 60;
    result->tm_hour = (t / 3600) % 24;
    result->tm_mday = 1;
    result->tm_mon = 0;
    result->tm_year = 70;
    result->tm_wday = 0;
    result->tm_yday = 0;
    result->tm_isdst = 0;
    return result;
}

/*
 * String functions
 */

long
strtol(const char *s, char **endptr, int base)
{
    int neg = 0;
    long val = 0;

    /* gobble initial whitespace */
    while (*s == ' ' || *s == '\t')
        s++;

    /* plus/minus sign */
    if (*s == '+')
        s++;
    else if (*s == '-')
        s++, neg = 1;

    /* hex or octal base prefix */
    if ((base == 0 || base == 16) && (s[0] == '0' && s[1] == 'x'))
        s += 2, base = 16;
    else if (base == 0 && s[0] == '0')
        s++, base = 8;
    else if (base == 0)
        base = 10;

    /* digits */
    while (1) {
        int dig;

        if (*s >= '0' && *s <= '9')
            dig = *s - '0';
        else if (*s >= 'a' && *s <= 'z')
            dig = *s - 'a' + 10;
        else if (*s >= 'A' && *s <= 'Z')
            dig = *s - 'A' + 10;
        else
            break;
        if (dig >= base)
            break;
        s++, val = (val * base) + dig;
    }

    if (endptr)
        *endptr = (char *) s;
    return (neg ? -val : val);
}

char *
strrchr(const char *cp, int ch)
{
    char *save;
    char c;

    for (save = (char *) 0; (c = *cp); cp++) {
        if (c == ch) {
            save = (char *) cp;
        }
    }
    return save;
}

/*
 * Random
 */

void
srand(unsigned int newseed)
{
    rand_seed = newseed;
}

long
random(void)
{
    /* Linear Congruential Generator (LCG) */
    rand_seed = (rand_seed * 1103515245 + 12345) % 0x7fffffff;
    return rand_seed;
}

/*
 * Platform initialization
 */

void
net_platform_init(void)
{
    mutex_init(&net_pendinglock);
    net_pending = 0;
    srand((unsigned int)r_time());
}

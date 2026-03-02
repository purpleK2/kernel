#ifndef SLEEP_H
#define SLEEP_H 1

#include <stdint.h>

#include <autoconf.h>

#define TICKS_PER_SEC  CONFIG_SCHED_TIMER_INTERVAL_MS * 1000
#define NS_PER_TICK    CONFIG_SCHED_TIMER_INTERVAL_MS * 1000000ULL

typedef struct timespec {
    int64_t tv_sec;
    int64_t tv_nsec;
} timespec_t;

void sleep_init(void);
void sleep_timer_check(void);
int do_nanosleep(const timespec_t *req, timespec_t *rem);

#endif // SLEEP_H

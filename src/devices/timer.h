#ifndef DEVICES_TIMER_H
#define DEVICES_TIMER_H

#include <round.h>
#include <stdint.h>
#include <list.h>

/* Number of timer interrupts per second. */
#define TIMER_FREQ 100
#define CLOCK_SIZE 67

/* A clock used to keep track of timer_sleeping threads */
struct clock {
    int64_t hand;
    struct list waiters[CLOCK_SIZE];
};

/* A timer used to contain the semaphore in the clock */
struct timer_elem {
    struct semaphore * sema;
    struct list_elem timer_elem;
    int64_t time_to_wake;
};

bool less_time_left(const struct list_elem *a,
                        const struct list_elem *b,
                        void *aux);
void timer_elem_init(struct timer_elem * t, struct semaphore * sema);
void clock_init (struct clock *clock);
void clock_add(struct clock *clock, int64_t ticks, struct timer_elem * t);
void clock_tick(struct clock *clock);

void timer_init (void);
void timer_calibrate (void);

int64_t timer_ticks (void);
int64_t timer_elapsed (int64_t);

/* Sleep and yield the CPU to other threads. */
void timer_sleep (int64_t ticks);
void timer_msleep (int64_t milliseconds);
void timer_usleep (int64_t microseconds);
void timer_nsleep (int64_t nanoseconds);

/* Busy waits. */
void timer_mdelay (int64_t milliseconds);
void timer_udelay (int64_t microseconds);
void timer_ndelay (int64_t nanoseconds);

void timer_print_stats (void);

#endif /* devices/timer.h */

#ifndef __KERNEL_TIMER_H__
#define __KERNEL_TIMER_H__

#include "kernel/types.h"

typedef struct timer
{
	void (*func)(void *arg);
	void *arg;
	uint64_t timeout_tick; // Changed to 64-bit for S-mode compatibility
	struct timer *next;
} timer;

/* interval ~= 1s, use generic timer frequency */
#define TIMER_INTERVAL 10000000UL

extern timer *timers, *next_timer;

extern uint64_t get_time(void); // Renamed from get_mtime, returns 64-bit time
extern void print_timers(void);
extern void wake_up_task(void *arg);
extern void timer_load(uint64_t); // Changed parameter type to 64-bit
extern void timer_init(void);
extern void timer_handler();
extern timer *timer_create(
	void (*handler)(void *arg),
	void *arg,
	uint32_t timeout);
extern void timer_delete(timer *timer);

#endif /* __KERNEL_TIMER_H__ */

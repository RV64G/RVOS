#ifndef __KERNEL_SCHED_H__
#define __KERNEL_SCHED_H__

#include "kernel/types.h"
#include "kernel/list.h"

/* task management */
struct context
{
	/* ignore x0 */
	reg_t ra;
	reg_t sp;
	reg_t gp;
	reg_t tp;
	reg_t t0;
	reg_t t1;
	reg_t t2;
	reg_t s0;
	reg_t s1;
	reg_t a0;
	reg_t a1;
	reg_t a2;
	reg_t a3;
	reg_t a4;
	reg_t a5;
	reg_t a6;
	reg_t a7;
	reg_t s2;
	reg_t s3;
	reg_t s4;
	reg_t s5;
	reg_t s6;
	reg_t s7;
	reg_t s8;
	reg_t s9;
	reg_t s10;
	reg_t s11;
	reg_t t3;
	reg_t t4;
	reg_t t5;
	reg_t t6;
	// upon is trap frame
	// save the pc to run in next schedule cycle
	reg_t pc;	   // offset: 31 * 8 = 248 (64-bit)
	reg_t sstatus; // S-mode status register (was mstatus) - offset: 32 * 8 = 256 (64-bit)
};

typedef enum
{
	TASK_INVALID,
	TASK_READY,
	TASK_RUNNING,
	TASK_SLEEPING,
	TASK_EXITED
} task_state;

struct task_struct
{
	struct context ctx;
	void *param;
	void (*start_routine)(void *param);
	uint8_t priority;
	task_state state;
	uint32_t timeslice;
	uint32_t remaining_timeslice;

	// Node for the run queue
	struct list_head run_queue_node;
};

#define DEFAULT_TIMESLICE 2
#define MAX_PRIORITY 32

/* scheduler functions */
void sched_init(void);
void schedule(void);
void kernel_scheduler(void);
int task_create(void (*start_routin)(void *param), void *param, uint8_t priority, uint32_t timeslice);
void task_delay(uint32_t ticks);
void task_yield(void);
void task_exit(int status);
int get_current_task_id(void);
void print_tasks(void);

/* global variables */
extern int current_task_id;
extern struct task_struct tasks[];

/* user tasks */
extern void user_task0(void *param);
extern void user_task1(void *param);
extern void user_task(void *param);

#endif /* __KERNEL_SCHED_H__ */
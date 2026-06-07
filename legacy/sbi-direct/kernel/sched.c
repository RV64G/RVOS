#include "kernel.h"
#include "kernel/spinlock.h"

/* defined in entry.S */
extern void switch_to(struct context *next);

#define MAX_TASKS 10
#define STACK_SIZE 4096		   // 增加到4KB
#define KERNEL_STACK_SIZE 4096 // 增加到4KB
// #define TASK_USABLE(i) (((tasks[(i)].state) == TASK_READY) || ((tasks[(i)].state) == TASK_RUNNING))
// S-mode status register definitions
#define SSTATUS_SPP_MASK (1 << 8) // Supervisor Previous Privilege
#define SSTATUS_SIE (1 << 1)	  // Supervisor Interrupt Enable
#define SSTATUS_SPIE (1 << 5)	  // Supervisor Previous Interrupt Enable
/*
 * In the standard RISC-V calling convention, the stack pointer sp
 * is always 16-byte aligned.
 */
uint8_t __attribute__((aligned(16))) task_stack[MAX_TASKS][STACK_SIZE];
uint8_t __attribute__((aligned(16))) kernel_stack_kernel[KERNEL_STACK_SIZE];
static struct spinlock sched_lock;
// __attribute__((aligned(16)))
struct task_struct tasks[MAX_TASKS];
static LIST_HEAD(run_queues[MAX_PRIORITY]);
static uint32_t run_queue_bitmap = 0;

static int tasks_count = 0;
int current_task_id = -1; // 当前任务ID，-1表示没有任务在运行

/*
 * _top is used to mark the max available position of tasks
 * _current is used to point to the context of current task
 */

void kernel_scheduler()
{
	while (1)
	{
		SCHEDULE;
	}
}

void back_to_os()
{
	schedule();
}

// 在 `sched_init` 中创建内核调度任务
/**
 * @brief 初始化调度器
 * @details
 *   此函数负责初始化任务数组、设置内核上下文以及启用软件中断，为任务调度做准备。
 *   它在 `start_kernel` 期间被调用一次。
 */
void sched_init()
{
	spinlock_init(&sched_lock, "sched_lock");
	w_sie(r_sie() | SIE_SSIE); // Enable supervisor software interrupts

	// Initialize the run queues and bitmap
	run_queue_bitmap = 0;
	for (int i = 0; i < MAX_PRIORITY; i++)
	{
		INIT_LIST_HEAD(&run_queues[i]);
	}

	// Initialize tasks array
	for (int i = 0; i < MAX_TASKS; i++)
	{
		tasks[i].state = TASK_INVALID;
	}

	// Initialize kernel context (though it's not used as a runnable task)
	struct context kernel_ctx; // This context is not used for scheduling
	kernel_ctx.sp = (reg_t)&kernel_stack_kernel[KERNEL_STACK_SIZE];
	kernel_ctx.ra = (reg_t)kernel_scheduler;
	kernel_ctx.pc = (reg_t)kernel_scheduler;
}

/**
 * @brief 从运行队列中选择下一个要运行的任务 (调度策略)。
 * @return 指向下一个任务的 task_struct 指针；如果没有就绪任务则返回 NULL。
 *
 * @details
 *   这是核心调度策略的实现，它结合了优先级和轮转调度：
 *   1. 使用位图 (`run_queue_bitmap`) 以 O(1) 的复杂度找到最高优先级的非空运行队列。
 *   2. 从该优先级队列的头部选择第一个任务。
 *   3. 为了实现同优先级任务间
的公平轮转 (Round-Robin)，
 *      将这个被选中的任务节点从队列头部移动到其所在队列的末尾。
 */
static struct task_struct *pick_next_task()
{
	if (run_queue_bitmap == 0)
	{
		return NULL; // 没有就绪任务
	}

	// 1. 使用编译器内置函数 `__builtin_ctz` (计算尾部零) 来 O(1) 地找到最高优先级。
	//    对于一个32位的整数，最低有效位的索引 = 尾部零的数量。
	//    优先级0最高，31最低。
	uint8_t highest_prio = __builtin_ctz(run_queue_bitmap);

	// 2. 从最高优先级队列的头部获取下一个任务节点。
	struct list_head *next_node = run_queues[highest_prio].next;
	struct task_struct *next_task = list_entry(next_node, struct task_struct, run_queue_node);

	// 3. 将被选中的任务移到其队列的末尾，以实现轮转。
	list_del(next_node);
	list_add_tail(next_node, &run_queues[highest_prio]);

	return next_task;
}

/**
 * @brief 调度器核心函数 (调度机制)。
 * @details
 *   此函数负责协调任务的切换。它遵循“策略与机制分离”的原则：
 *   1. 将当前正在运行的任务（如果存在）的状态设置为就绪。
 *   2. 调用 `pick_next_task()` (策略) 来决策出下一个应该运行的任务。
 *   3. 更新全局的任务状态。
 *   4. 调用 `switch_to()` (机制) 来执行底层的上下文切换。
 */
void schedule()
{
	struct task_struct *current_task = NULL;
	struct task_struct *next_task = NULL;

	// 1. 获取当前任务，如果它正在运行，则将其状态更新为就绪。
	//    注意：我们不需要把它重新加入运行队列，因为 `pick_next_task` 内部的
	//    轮换机制已经把它从队列头移到了队列尾，它仍然在队列中。
	if (current_task_id != -1)
	{
		current_task = &tasks[current_task_id];
		if (current_task->state == TASK_RUNNING)
		{
			current_task->state = TASK_READY;
		}
	}

	// 2. 策略：选择下一个要运行的任务。
	next_task = pick_next_task();

	if (next_task == NULL)
	{
		// 在一个有空闲任务（idle task）的成熟系统中，这里不应该发生。
		// 目前我们暂时触发 panic。
		panic("No ready tasks to schedule!");
		return;
	}

	// 3. 更新全局状态。
	//    通过指针减法，从任务的地址计算出它在 `tasks` 数组中的索引。
	current_task_id = next_task - tasks;
	next_task->state = TASK_RUNNING;

	// 4. 机制：执行上下文切换。
	//    仅当选择出的下一个任务与当前任务不同时，才执行切换。
	//    这是一种优化，避免了不必要的上下文保存和恢复。
	if (current_task != next_task)
	{
		switch_to(&next_task->ctx);
	}
}

// void check_timeslice()
// {
// 	tasks[current_ctx].remaining_timeslice--;
// 	if (tasks[current_ctx].remaining_timeslice == 0)
// 	{
// 		tasks[current_ctx].remaining_timeslice = tasks[current_ctx].timeslice;
// 		task_yield();
// 	}
// }

/**
 * @brief 创建一个新任务
 * @details
 *   此函数在 `tasks` 数组中找到一个空闲槽位，并为其设置任务入口点、栈、优先级和初始上下文。
 *   关键步骤是设置 `ctx.sstatus` 寄存器，通过清除 `SSTATUS_SPP` 位来确保任务在用户模式下运行。
 *
 * @param start_routin 任务的入口函数指针
 * @param param 传递给任务入口函数的参数
 * @param priority 任务优先级 (数字越小，优先级越高)
 * @param timeslice 任务的时间片大小
 * @return 成功则返回任务ID，失败返回-1
 *
 * @callgraph
 *   - `os_main()` (kernel/main.c)
 *     - `task_create(user_task, ...)`
 *   - `task_create` (self)
 *     - 分配任务结构体 `task_t`
 *     - 设置任务栈 `task_stack`
 *     - 设置PC指向 `start_routin`
 *     - 设置 `sstatus` 以切换到用户态
 *     - 将任务状态设置为 `TASK_READY`
 */
int task_create(void (*start_routine)(void *param), void *param, uint8_t priority, uint32_t timeslice)
{
	spinlock_acquire(&sched_lock);

	int task_id = -1;
	for (int i = 0; i < MAX_TASKS; i++)
	{
		if (tasks[i].state == TASK_INVALID)
		{
			task_id = i;
			break;
		}
	}

	if (task_id == -1)
	{
		spinlock_release(&sched_lock);
		printk("Task creation failed: No free task slot.\n");
		return -1;
	}

	if (priority >= MAX_PRIORITY)
	{
		spinlock_release(&sched_lock);
		printk("Task creation failed: Invalid priority %d.\n", priority);
		return -1;
	}

	struct task_struct *new_task = &tasks[task_id];

	new_task->start_routine = start_routine;
	new_task->param = param;
	// Fix: Stack pointer must be 16-byte aligned. 
    // Point to the byte *after* the array, which is the valid Top of Stack for a downward growing stack.
	new_task->ctx.sp = (reg_t)task_stack[task_id] + STACK_SIZE;
	new_task->ctx.pc = (reg_t)start_routine;
	new_task->ctx.a0 = (reg_t)param;

	uint32_t sstatus = r_sstatus();
	sstatus &= ~SSTATUS_SPP_MASK;
	sstatus |= SSTATUS_SPIE;
	new_task->ctx.sstatus = sstatus;

	new_task->priority = priority;
	new_task->state = TASK_READY;
	new_task->timeslice = timeslice;
	new_task->remaining_timeslice = timeslice;

	// Add the new task to the correct priority run queue
	list_add_tail(&new_task->run_queue_node, &run_queues[priority]);
	run_queue_bitmap |= (1U << priority);

	printk("Task %d created: PC=0x%lx, SP=0x%lx, Prio=%d\n", task_id, new_task->ctx.pc, new_task->ctx.sp, priority);

	spinlock_release(&sched_lock);
	return task_id;
}

/*
 * DESCRIPTION
 *  task_yield() causes the calling task to relinquish the CPU and a new
 *  task gets to run.
 */
void task_yield()
{
	spinlock_acquire(&sched_lock);
	if (current_task_id != -1 && tasks[current_task_id].state == TASK_RUNNING)
	{
		tasks[current_task_id].state = TASK_READY;
	}
	spinlock_release(&sched_lock);

	schedule();
}
/**
 * @brief 获取当前硬件线程ID (Hart ID) 的核心实现。
 * @details
 *   这是一个内核内部函数，负责读取当前CPU的Hart ID。
 *   它不进行参数验证，假定调用者已经处理了用户态指针的合法性。
 * @param ptr_hid 指向存储Hart ID的用户态地址。
 * @return 成功返回0，失败返回-1。
 * @callgraph
 *   - `sys_gethid()` (kernel/syscall.c)
 */
long do_gethid(unsigned int *ptr_hid)
{
	if (ptr_hid == 0)
	{
		// 理论上，sys_gethid 应该已经验证过，但这里作为核心逻辑，可以保留防御性检查
		printk("do_gethid: ptr_hid == NULL\n");
		return -1;
	}
	*ptr_hid = r_tp(); // r_tp() 是获取Hart ID的RISC-V指令
	return 0;
}

/**
 * @brief 终止当前任务的执行。
 * @param status 任务的退出状态码 (目前尚未使用)。
 *
 * @details
 *   此函数负责处理任务退出的所有核心逻辑：
 *   1. 从其所在的运行队列中移除当前任务。
 *   2. 检查该队列是否因此变空，如果为空，则更新运行队列位图。
 *   3. 将任务状态标记为 EXITED。
 *   4. 清理全局的 current_task_id。
 *   5. 立即调用 schedule() 来调度一个新任务，此函数不会返回。
 */
void task_exit(int status)
{
	spinlock_acquire(&sched_lock);
	if (current_task_id != -1)
	{
		struct task_struct *current_task = &tasks[current_task_id];
		uint8_t prio = current_task->priority;

		// 1. (关键修复) 将当前任务从其运行队列中移除。
		list_del(&current_task->run_queue_node);

		// 2. 如果该优先级的队列因此变空，则清除位图中对应的 bit 位。
		if (list_empty(&run_queues[prio]))
		{
			run_queue_bitmap &= ~(1U << prio);
		}

		// 3. 更新任务状态并清理全局 ID。
		current_task->state = TASK_EXITED;
		printk("Task %d exited with status %d.\n", current_task_id, status);
		current_task_id = -1;
	}
	spinlock_release(&sched_lock);

	// 4. 立即调度一个新任务，此函数将不再返回。
	schedule();

	// 理论上不应该执行到这里。
	panic("task_exit: schedule() returned!");
}

// 定时器回调函数，用于唤醒被延迟的任务
void wake_up_task(void *arg)
{
	uintptr_t task_id = (uintptr_t)arg;

	if (task_id < MAX_TASKS && tasks[task_id].state == TASK_SLEEPING)
	{
		struct task_struct *task = &tasks[task_id];
		uint8_t prio = task->priority;

		task->state = TASK_READY;
		list_add_tail(&task->run_queue_node, &run_queues[prio]);
		run_queue_bitmap |= (1U << prio);
	}
}

// void task_go(int i)
// {
// 	current_ctx = &tasks[i];
// 	// switch_to(ctx_now);
// 	sys_switch(&kernel_ctx, &tasks[i]);
// }

/*
 * DESCRIPTION
 *  task_delay() causes the calling task to sleep for a specified number of ticks.
 *  - ticks: 延迟的时钟周期数
 */
void task_delay(uint32_t ticks)
{
	spinlock_acquire(&sched_lock);
	if (current_task_id == -1)
	{
		spinlock_release(&sched_lock);
		return;
	}

	struct task_struct *current_task = &tasks[current_task_id];
	uint8_t prio = current_task->priority;

	// 1. Remove from the run queue
	list_del(&current_task->run_queue_node);

	// 2. Update bitmap if the queue becomes empty
	if (list_empty(&run_queues[prio]))
	{
		run_queue_bitmap &= ~(1U << prio);
	}

	// 3. Set state to sleeping
	current_task->state = TASK_SLEEPING;

	// 4. Create a timer to wake up the task
	//    We pass the task_id as an integer value via the pointer argument.
	uintptr_t task_id_val = (uintptr_t)current_task_id;
	if (timer_create(wake_up_task, (void *)task_id_val, ticks) == NULL)
	{
		// If timer creation fails, put the task back on the run queue.
		list_add_tail(&current_task->run_queue_node, &run_queues[prio]);
		run_queue_bitmap |= (1U << prio);
		current_task->state = TASK_READY;
	}

	spinlock_release(&sched_lock);

	// 5. Yield the CPU
	schedule();
}

/**
 * @brief 获取当前任务ID
 * @return 当前任务的ID，如果没有当前任务则返回-1
 */
int get_current_task_id()
{
	return current_task_id;
}

/* 获取任务函数名称 */
static const char *get_task_func_name(void (*func)(void *))
{
	if (func == user_task0)
		return "user_task0";
	if (func == user_task1)
		return "user_task1";
	if (func == user_task)
		return "user_task";
	if (func == (void (*)(void *))timer_handler)
		return "timer_handler";
	if ((void (*)(void))func == task_yield)
		return "task_yield";
	return "unknown";
}

/* 打印任务槽信息的调试函数 */
void print_tasks()
{
	printk("\n=== Tasks Debug Info ===\n");

	int active_tasks = 0;
	for (int i = 0; i < MAX_TASKS; i++)
	{
		if (tasks[i].state != TASK_INVALID)
		{
			printk("Task[%d]:\n", i);
			printk("  Function: %s\n", get_task_func_name(tasks[i].start_routine));
			if (tasks[i].start_routine == user_task)
			{
				int task_id = (int)(intptr_t)(tasks[i].param);
				printk("  Task ID: %d\n", task_id);
			}
			printk("  Priority: %d\n", tasks[i].priority);

			const char *state_str;
			switch (tasks[i].state)
			{
			case TASK_READY:
				state_str = "READY";
				break;
			case TASK_RUNNING:
				state_str = "RUNNING";
				break;
			case TASK_SLEEPING:
				state_str = "SLEEPING";
				break;
			case TASK_EXITED:
				state_str = "EXITED";
				break;
			default:
				state_str = "UNKNOWN";
				break;
			}
			printk("  State: %s\n", state_str);
			if (i == current_task_id)
			{
				printk("  [CURRENT]\n");
			}
			printk("------------------\n");
			active_tasks++;
		}
	}
	printk("Active tasks: %d, Current: %d\n", active_tasks, current_task_id);
	printk("=== End of Tasks Info ===\n\n");
}

#ifndef KERNEL_SCHED_H
#define KERNEL_SCHED_H

struct trap_frame;
struct task;

/**
 * 初始化调度器，把 boot task 作为当前正在运行的 task。
 */
void sched_init(struct task *boot_task);

/**
 * 清空调度器全局状态。
 *
 * 只供 selftest 清理临时 task 使用；正式内核初始化后不会 reset scheduler。
 */
void sched_reset(void);

/**
 * 把 READY task 放入运行队列。
 */
void sched_enqueue(struct task *task);

/**
 * 主动让出 CPU。
 *
 * 这是内核 task 的协作式调度入口，底层走 context_switch()。
 */
void sched_yield(void);

/**
 * 请求在 trap 返回前调度。
 */
void sched_request_reschedule(void);

/**
 * trap 返回路径上的调度入口。
 *
 * 返回 trap.S 应该恢复的 trap_frame。
 */
struct trap_frame *sched_from_trap(struct trap_frame *frame);

struct task *sched_current(void);

#endif

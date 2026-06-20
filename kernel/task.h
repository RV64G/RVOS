#ifndef KERNEL_TASK_H
#define KERNEL_TASK_H

#include <stdint.h>

#include "context.h"
#include "list.h"
#include "timer.h"

struct trap_frame;
struct vm_space;

enum task_state
{
    TASK_UNUSED,
    TASK_READY,
    TASK_RUNNING,
    TASK_BLOCKED,
    TASK_ZOMBIE,
};

typedef void (*task_entry_t)(void *arg);

struct task
{
    uint64_t id;
    enum task_state state;
    struct context context;
    void *kernel_stack;
    uint64_t kernel_stack_size;
    task_entry_t entry;
    void *arg;
    const char *name;
    struct trap_frame *trap_frame;
    struct vm_space *vm_space;
    struct timer_event sleep_timer;
    struct list_head run_node;
    int queued;
};

/**
 * 初始化内核任务系统，把当前执行流登记成 boot task。
 */
void task_system_init(struct task *boot_task, const char *name);

/**
 * 清空当前 task 系统状态。
 *
 * 主要用于自检结束后移除指向栈上 task 对象的全局指针。正式调度器接入后，正常运行
 * 路径不会频繁 reset。
 */
void task_system_reset(void);

/**
 * 创建一个内核 task。
 *
 * kernel_stack 由调用者提供，大小必须足够且按 16 字节对齐后的栈顶可用。第一版只支持
 * 内核态任务，不创建用户地址空间，也不保存用户 trap frame。
 */
int task_create(
    struct task *task,
    const char *name,
    void *kernel_stack,
    uint64_t kernel_stack_size,
    task_entry_t entry,
    void *arg
);

/**
 * 主动让出 CPU，切换到下一个 READY task。
 */
void task_yield(void);

/**
 * 请求当前 task 在 trap 返回前让出 CPU。
 *
 * 用户态 syscall 不能直接走 context_switch()，因为当前执行流还在 trap 栈上。这个
 * 接口只设置调度请求，真正切换发生在 trap.S 恢复现场之前。
 */
void task_yield_from_trap(void);

/**
 * 阻塞当前 task，直到指定毫秒数后被 timer 唤醒。
 *
 * 当前实现要求还有其它 READY task 可以运行；正式 idle task 出现前，不应该让系统里
 * 唯一的可运行 task sleep。
 */
int task_sleep_ms(uint64_t milliseconds);

/**
 * 在 trap 返回路径上阻塞当前 task，直到指定毫秒数后被 timer 唤醒。
 */
int task_sleep_ms_from_trap(uint64_t milliseconds);

/**
 * 把当前 task 标记为 ZOMBIE，并请求 trap 返回前切到其它 READY task。
 */
void task_exit_from_trap(void);

/**
 * 把 BLOCKED task 唤醒并重新放回 run queue。
 */
void task_wake(struct task *task);

struct task *task_current(void);

#endif

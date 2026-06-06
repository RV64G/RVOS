#ifndef KERNEL_TIMER_H
#define KERNEL_TIMER_H

#include <stdint.h>

struct timer_event;

typedef void (*timer_callback_t)(struct timer_event *event, void *context); // 看到这里立刻重温typedef用法

struct timer_event
{
    struct timer_event *next;
    uint64_t deadline_cycles;
    uint64_t period_cycles;
    timer_callback_t callback;
    void *context;
    int active;
};

/**
 * 初始化 S-mode timer。
 *
 * 当前通过 SBI set_timer 设置下一次 supervisor timer interrupt。硬件 timebase_frequency
 * 来自 DTB 解析后的 platform_info。
 */
int timer_init(void);

/**
 * 初始化一个由调用者持有的 timer 节点。
 *
 * timer 子系统不会为事件节点分配或释放内存。后续 PCB、sleep 队列、IO 超时等对象
 * 可以把 struct timer_event 直接嵌进自己的结构体里，避免在中断上下文里 kmalloc。
 */
void timer_event_init(struct timer_event *event, timer_callback_t callback, void *context);

/**
 * 按毫秒调度 timer 事件。
 *
 * delay_ms 表示多少毫秒后触发。period_ms 为 0 时是一次性 timer；非 0 时是周期
 * timer。接口使用毫秒，内部会换算成 rdtime cycles。
 */
int timer_schedule_ms(struct timer_event *event, uint64_t delay_ms, uint64_t period_ms);

/**
 * 从 timer list 中取消事件。
 *
 * 如果事件已经触发且不在链表里，函数直接返回。
 */
void timer_cancel(struct timer_event *event);

/**
 * 把毫秒转换成 rdtime cycles，结果至少为 1。
 */
uint64_t timer_ms_to_cycles(uint64_t milliseconds);

/**
 * 处理 supervisor timer interrupt。
 *
 * trap 分发层在 scause 表示 timer interrupt 时调用它。函数会执行已经到期的 timer
 * list 回调，并设置下一次硬件 timer interrupt。
 */
void timer_handle_interrupt(void);

#endif

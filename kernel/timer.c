#include "timer.h"

#include "csr.h"
#include "platform.h"
#include "printk.h"
#include "riscv/sbi.h"

#define SSTATUS_SIE (1ULL << 1)
#define SIE_STIE (1ULL << 5)

static uint64_t timebase_frequency;
static struct timer_event *timer_list;

static void timer_run_due_events(void);
static int timer_schedule_cycles(struct timer_event *event,
                                 uint64_t delay_cycles, uint64_t period_cycles);

static uint64_t timer_saturating_add(uint64_t left, uint64_t right)
{
    if (left > UINT64_MAX - right)
    {
        return UINT64_MAX;
    }
    return left + right;
}

static void timer_insert(struct timer_event *event)
{
    struct timer_event **slot = &timer_list;

    /*
     * timer_list 按 deadline_cycles 从小到大排序。插入时维护这个顺序，timer
     * interrupt 只需要从链表头开始处理，遇到第一个未到期事件就可以停止。
     */
    while (*slot && (*slot)->deadline_cycles <= event->deadline_cycles)
    {
        slot =
            &(*slot)
                 ->next; // 注意这里slot存的是next这个指针的地址而不是next指针的值
    }

    event->next =
        *slot; // 若是开头插入，*slot就是原来的头节点（指针，别忘了timerlist也是指针，指向最近过期的event）；若是中间插入，*slot就是前一个节点的next指针（注意是指针值，因此event在这里可以成功地复制pre的next指针）；若是结尾插入，*slot就是NULL
    *slot =
        event; // 开头的话，timerlist就被*slot改写；中间插入的话，把slot理解成pre->next就会好很多，next指针就会被指向event
    event->active = 1;
}

static void timer_remove(struct timer_event *event)
{
    struct timer_event **slot = &timer_list;

    while (*slot)
    {
        if (*slot == event)
        {
            *slot = event->next;
            event->next = 0;
            event->active = 0;
            return;
        }
        slot = &(*slot)->next;
    }
}

static void timer_program_next(void)
{
    /*
     * SBI 的下一次 deadline 来自 timer_list 头节点。没有事件时使用 UINT64_MAX，
     * 让 timer 尽量晚触发。
     *
     * 只要 timer list 的头节点可能变化，就需要重新调用这个函数：新增更早的
     * timer、 取消当前最早 timer、或者 timer interrupt 处理完到期事件之后。
     */
    uint64_t deadline = timer_list ? timer_list->deadline_cycles : UINT64_MAX;

    sbi_set_timer(deadline);
}

static void timer_reschedule_periodic_after(struct timer_event *event,
                                            uint64_t now)
{
    /*
     * 周期 timer 按原 deadline 推进，而不是使用 now +
     * period。这样短暂延迟不会造成
     * 长期漂移；如果错过多个周期，只执行一次回调，并把下一次 deadline
     * 推进到未来。
     */
    do
    {
        event->deadline_cycles =
            timer_saturating_add(event->deadline_cycles, event->period_cycles);
    } while (event->deadline_cycles <= now &&
             event->deadline_cycles != UINT64_MAX);
}

static void timer_run_due_events(void)
{
    /*
     * now 是本次 timer interrupt 的批处理边界，不在 while 中刷新。
     *
     * 如果每处理一个回调就重新读取 rdtime，某个耗时回调可能让同一个短周期 timer
     * 在一次中断里反复“追赶”执行，导致中断处理时间失控。当前策略是：只处理进入
     * handler 时已经到期的事件；回调执行期间新到期的事件交给下一次 timer
     * interrupt。
     */
    uint64_t now = sbi_get_time();

    while (timer_list && timer_list->deadline_cycles <= now)
    {
        struct timer_event *event = timer_list;
        timer_list = event->next;
        event->next = 0;
        event->active = 0;

        uint64_t scheduled_deadline = 0;

        if (event->period_cycles != 0)
        {
            /*
             * 周期 timer
             * 先重新挂回链表，再执行回调。这样回调如果决定停止周期任务，
             * 可以直接调用 timer_cancel(event)。
             */
            timer_reschedule_periodic_after(event, now);
            scheduled_deadline = event->deadline_cycles;
            timer_insert(event);
        }

        if (event->callback)
        {
            event->callback(event, event->context);
        }

        /*
         * 回调可能很慢。它执行完以后，刚才重挂的周期 timer 可能又落在当前
         * rdtime 之前；如果直接把这个过期 deadline 交给 SBI，下一次 timer
         * interrupt 会立刻 到来，形成“慢回调 -> 过期 deadline ->
         * 立刻再中断”的追赶风暴。
         *
         * 只在 event 仍然 active 且 deadline
         * 没被回调改过时调整它：如果回调调用了 timer_cancel()，active 会变成
         * 0；如果回调重新 schedule，deadline 会变化。
         */
        if (event->period_cycles != 0 && event->active &&
            event->deadline_cycles == scheduled_deadline)
        {
            uint64_t after_callback = sbi_get_time();
            if (event->deadline_cycles <= after_callback)
            {
                timer_remove(event);
                timer_reschedule_periodic_after(event, after_callback);
                timer_insert(event);
            }
        }
    }
}

int timer_init(void)
{
    const struct platform_info *platform = platform_info();

    if (platform->timebase_frequency == 0)
    {
        printk("Timer init failed: missing timebase_frequency\r\n");
        return 0;
    }

    timebase_frequency = platform->timebase_frequency;
    timer_list = 0;

    printk("SBI timer ready\r\n");
    printk_dec_field("timebase_frequency", timebase_frequency);
    printk_field("rdtime", sbi_get_time());

    timer_program_next();
    csr_set_sie(SIE_STIE);
    csr_set_sstatus(SSTATUS_SIE);
    return 1;
}

void timer_event_init(struct timer_event *event, timer_callback_t callback,
                      void *context)
{
    // 方便event复用
    if (!event)
    {
        return;
    }

    event->next = 0;
    event->deadline_cycles = 0;
    event->period_cycles = 0;
    event->callback = callback;
    event->context = context;
    event->active = 0;
}

static int timer_schedule_cycles(struct timer_event *event,
                                 uint64_t delay_cycles, uint64_t period_cycles)
{
    if (!event || !event->callback || timebase_frequency == 0)
    {
        return 0;
    }

    if (delay_cycles == 0)
    {
        delay_cycles = 1;
    }

    if (event->active)
    {
        timer_remove(event);
    }

    event->deadline_cycles = timer_saturating_add(sbi_get_time(), delay_cycles);
    event->period_cycles = period_cycles;
    timer_insert(event);
    timer_program_next();
    return 1;
}

int timer_schedule_ms(struct timer_event *event, uint64_t delay_ms,
                      uint64_t period_ms)
{
    return timer_schedule_cycles(
        event, timer_ms_to_cycles(delay_ms),
        period_ms == 0 ? 0 : timer_ms_to_cycles(period_ms));
}

void timer_cancel(struct timer_event *event)
{
    if (!event || !event->active)
    {
        return;
    }

    timer_remove(event);
    timer_program_next();
}

uint64_t timer_ms_to_cycles(uint64_t milliseconds)
{
    if (timebase_frequency == 0)
    {
        return 0;
    }

    /*
     * 不直接计算 milliseconds * timebase_frequency，避免长延时在 64
     * 位乘法里溢出。 先拆成整秒和剩余毫秒。
     *
     * 不能简单使用 milliseconds * (timebase_frequency / 1000)。如果 timebase 是
     * 32768Hz，整数 cycles/ms 是
     * 32，但真实值是 32.768；先截断再乘会让误差随毫秒 数累积。这里等价于计算
     * ceil(milliseconds * timebase_frequency / 1000)，只是 通过“整秒 +
     * 剩余毫秒”避免大乘法溢出。
     */
    uint64_t seconds = milliseconds / 1000;
    uint64_t remaining_ms = milliseconds % 1000;

    if (seconds > UINT64_MAX / timebase_frequency)
    {
        return UINT64_MAX;
    }

    uint64_t cycles = seconds * timebase_frequency;
    /*
     * 剩余毫秒部分：
     *
     * remaining_ms 一定小于 1000。现实平台的 timebase_frequency 不会接近
     * UINT64_MAX / 999，所以这里直接先乘 timebase_frequency，再除以
     * 1000。这样比 先算整数 cycles/ms 再乘更准确，不会丢掉 32768Hz 这类
     * timebase 的小数部分。
     *
     * 使用除法和取余做向上取整，避免 (product + 999) 这种写法在 product 接近
     * UINT64_MAX 时溢出。向上取整的目的不是更“精确”，而是保证 timer 不会早于
     * 请求时间触发。
     */
    uint64_t remainder_product = remaining_ms * timebase_frequency;
    uint64_t extra_cycles = remainder_product / 1000;
    if ((remainder_product % 1000) != 0)
    {
        extra_cycles++;
    }

    if (cycles > UINT64_MAX - extra_cycles)
    {
        return UINT64_MAX;
    }

    cycles += extra_cycles;
    if (cycles == 0)
    {
        return 1;
    }
    return cycles;
}

void timer_handle_interrupt(void)
{
    timer_run_due_events();
    timer_program_next();
}

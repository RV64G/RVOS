#include "task.h"

#include "align.h"
#include "early_log.h"
#include "sched.h"
#include "string.h"
#include "trap.h"

#define SSTATUS_SPIE (1ULL << 5)
#define SSTATUS_SPP  (1ULL << 8)

static uint64_t next_task_id;

static void task_sleep_timeout(struct timer_event *event, void *context)
{
    (void)event;

    task_wake((struct task *)context);
}

static void task_trampoline(void)
{
    struct task *task = sched_current();
    task->entry(task->arg);
    task->state = TASK_ZOMBIE;
    task_yield();

    /*
     * 如果没有其它 READY task 可以切换，zombie task 不能继续向下返回。这里停机说明
     * 调用侧没有安排好回收/等待路径。
     */
    early_halt_forever();
}

void task_system_init(struct task *boot_task, const char *name)
{
    next_task_id = 1;

    boot_task->id = next_task_id++;
    boot_task->state = TASK_RUNNING;
    memzero(&boot_task->context, sizeof(boot_task->context));
    boot_task->kernel_stack = 0;
    boot_task->kernel_stack_size = 0;
    boot_task->entry = 0;
    boot_task->arg = 0;
    boot_task->name = name;
    boot_task->trap_frame = 0;
    timer_event_init(&boot_task->sleep_timer, task_sleep_timeout, boot_task);
    list_init(&boot_task->run_node);
    boot_task->queued = 0;
    sched_init(boot_task);
}

void task_system_reset(void)
{
    next_task_id = 0;
    sched_reset();
}

int task_create(
    struct task *task,
    const char *name,
    void *kernel_stack,
    uint64_t kernel_stack_size,
    task_entry_t entry,
    void *arg
)
{
    if (!task || !kernel_stack || kernel_stack_size < 256 || !entry ||
        !sched_current())
    {
        return 0;
    }

    uint64_t stack_base = (uint64_t)(uintptr_t)kernel_stack;
    uint64_t stack_top = align_down(stack_base + kernel_stack_size, 16);
    if (stack_top <= stack_base + sizeof(struct trap_frame))
    {
        return 0;
    }

    struct trap_frame *initial_frame =
        (struct trap_frame *)(uintptr_t)(stack_top - sizeof(struct trap_frame));

    task->id = next_task_id++;
    task->state = TASK_READY;
    memzero(&task->context, sizeof(task->context));
    task->context.ra = (uint64_t)(uintptr_t)task_trampoline;
    task->context.sp = stack_top;
    memzero(initial_frame, sizeof(*initial_frame));
    initial_frame->sepc = (uint64_t)(uintptr_t)task_trampoline;
    initial_frame->sp = stack_top;
    initial_frame->sstatus = SSTATUS_SPP | SSTATUS_SPIE;
    task->kernel_stack = kernel_stack;
    task->kernel_stack_size = kernel_stack_size;
    task->entry = entry;
    task->arg = arg;
    task->name = name;
    task->trap_frame = initial_frame;
    timer_event_init(&task->sleep_timer, task_sleep_timeout, task);
    list_init(&task->run_node);
    task->queued = 0;
    sched_enqueue(task);
    return 1;
}

void task_yield(void)
{
    sched_yield();
}

int task_sleep_ms(uint64_t milliseconds)
{
    struct task *task = sched_current();
    if (!task || task->state != TASK_RUNNING)
    {
        return 0;
    }

    /*
     * sleep_timer 嵌在 task 里，timer 中断到期时不需要分配或释放节点，只把对应
     * task 从 BLOCKED 改回 READY。
     */
    task->state = TASK_BLOCKED;
    if (!timer_schedule_ms(&task->sleep_timer, milliseconds, 0))
    {
        task->state = TASK_RUNNING;
        return 0;
    }

    sched_yield();
    return 1;
}

void task_wake(struct task *task)
{
    if (!task || task->state != TASK_BLOCKED)
    {
        return;
    }

    task->state = TASK_READY;
    sched_enqueue(task);
}

struct task *task_current(void)
{
    return sched_current();
}

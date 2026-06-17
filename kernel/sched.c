#include "sched.h"

#include "context.h"
#include "list.h"
#include "task.h"

static struct task *current_task;
static LIST_HEAD(run_queue);
static int reschedule_requested;

static struct task *pick_next_task(void)
{
    if (list_empty(&run_queue))
    {
        return 0;
    }

    struct task *next = list_first_entry(&run_queue, struct task, run_node);
    list_del(&next->run_node);
    next->queued = 0;
    return next;
}

static void make_current_ready(void)
{
    if (current_task && current_task->state == TASK_RUNNING)
    {
        current_task->state = TASK_READY;
        sched_enqueue(current_task);
    }
}

void sched_init(struct task *boot_task)
{
    list_init(&run_queue);
    current_task = boot_task;
    reschedule_requested = 0;
}

void sched_reset(void)
{
    list_init(&run_queue);
    current_task = 0;
    reschedule_requested = 0;
}

void sched_enqueue(struct task *task)
{
    if (!task || task->state != TASK_READY || task->queued)
    {
        return;
    }

    list_add_tail(&task->run_node, &run_queue);
    task->queued = 1;
}

void sched_yield(void)
{
    struct task *old = current_task;
    struct task *next = pick_next_task();
    if (!old || !next)
    {
        return;
    }

    make_current_ready();
    next->state = TASK_RUNNING;
    current_task = next;
    context_switch(&old->context, &next->context);
}

void sched_request_reschedule(void)
{
    reschedule_requested = 1;
}

struct trap_frame *sched_from_trap(struct trap_frame *frame)
{
    if (!reschedule_requested || !current_task || !frame)
    {
        return frame;
    }

    reschedule_requested = 0;
    struct task *old = current_task;
    struct task *next = pick_next_task();
    if (!next || !next->trap_frame)
    {
        return frame;
    }

    /*
     * trap.S 已经把当前 task 的完整现场压成 frame。这里只更新调度状态，真正恢复
     * 哪个 task 由 trap.S 根据返回的 trap_frame 决定。
     */
    old->trap_frame = frame;
    make_current_ready();
    next->state = TASK_RUNNING;
    current_task = next;
    return next->trap_frame;
}

struct task *sched_current(void)
{
    return current_task;
}

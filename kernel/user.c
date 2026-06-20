#include "user.h"

#include <stdint.h>

#include "console.h"
#include "early_log.h"
#include "early_vm.h"
#include "page_alloc.h"
#include "platform.h"
#include "printk.h"
#include "sched.h"
#include "task.h"
#include "timer.h"
#include "trap.h"
#include "user_elf.h"
#include "vm.h"

/*
 * Sv39 低半区最大到 0x3f_ffff_ffff。用户 demo 栈放在低半区顶部附近，避开当前
 * 内核复制进用户页表的低地址 direct map。
 */
#define USER_STACK_TOP  0x0000003ffff00000ULL
#define USER_STACK_PAGES 2ULL
#define USER_TRAP_STACK_PAGES 2ULL
#define USER_KERNEL_STACK_PAGES 4ULL
#define USER_DEMO_TASK_COUNT 3ULL
#define USER_IDLE_STACK_PAGES 2ULL
#define USER_IDLE_STATUS_PERIOD_MS 2000ULL

extern char __user_elf_start[];
extern char __user_elf_end[];
extern void riscv_enter_user(uint64_t entry, uint64_t stack_top,
                             uint64_t trap_stack_top, uint64_t arg0,
                             uint64_t arg1, uint64_t arg2, uint64_t arg3);

struct user_task_start
{
    uint64_t entry;
    uint64_t user_stack_top;
    uint64_t trap_stack_top;
    uint64_t arg0;
    uint64_t arg1;
    uint64_t arg2;
    uint64_t arg3;
};

static struct task user_tasks[USER_DEMO_TASK_COUNT];
static struct vm_space user_vms[USER_DEMO_TASK_COUNT];
static struct user_task_start user_starts[USER_DEMO_TASK_COUNT];
static struct task idle_task;
static struct timer_event idle_status_timer;
static volatile int idle_status_due;

static const char *task_state_name(enum task_state state)
{
    switch (state)
    {
    case TASK_UNUSED:
        return "unused";
    case TASK_READY:
        return "ready";
    case TASK_RUNNING:
        return "running";
    case TASK_BLOCKED:
        return "blocked";
    case TASK_ZOMBIE:
        return "zombie";
    default:
        return "unknown";
    }
}

static void print_task_line(const struct task *task)
{
    printk("  task id=");
    printk_u64(task->id);
    printk(" name=");
    printk(task->name ? task->name : "(null)");
    printk(" state=");
    printk(task_state_name(task->state));
    printk("\r\n");
}

static void print_cycles_us(const char *cycles_name, const char *us_name,
                            uint64_t cycles)
{
    const struct platform_info *platform = platform_info();

    printk_dec_field(cycles_name, cycles);
    if (platform->timebase_frequency != 0)
    {
        uint64_t us = (cycles * 1000000ULL) / platform->timebase_frequency;
        printk_dec_field(us_name, us);
    }
}

static void print_user_demo_status(void)
{
    struct trap_stats stats;

    printk("User demo task status\r\n");
    for (uint64_t i = 0; i < USER_DEMO_TASK_COUNT; i++)
    {
        print_task_line(&user_tasks[i]);
    }
    print_task_line(&idle_task);

    trap_stats_snapshot(&stats);
    printk("Trap statistics\r\n");
    printk_dec_field("trap_total_count", stats.total_count);
    print_cycles_us("trap_total_max_cycles", "trap_total_max_us",
                    stats.total_max_cycles);
    printk_dec_field("trap_syscall_count", stats.syscall_count);
    print_cycles_us("trap_syscall_max_cycles", "trap_syscall_max_us",
                    stats.syscall_max_cycles);
    printk_dec_field("trap_yield_count", stats.syscall_yield_count);
    print_cycles_us("trap_yield_max_cycles", "trap_yield_max_us",
                    stats.syscall_yield_max_cycles);
    printk_dec_field("trap_interrupt_count", stats.interrupt_count);
    print_cycles_us("trap_interrupt_max_cycles", "trap_interrupt_max_us",
                    stats.interrupt_max_cycles);
    printk_dec_field("trap_timer_count", stats.timer_count);
    print_cycles_us("trap_timer_max_cycles", "trap_timer_max_us",
                    stats.timer_max_cycles);
    printk_dec_field("trap_external_count", stats.external_count);
    print_cycles_us("trap_external_max_cycles", "trap_external_max_us",
                    stats.external_max_cycles);
}

static void idle_status_timeout(struct timer_event *event, void *context)
{
    (void)event;
    (void)context;

    idle_status_due = 1;
}

static int load_user_image(struct vm_space *space, uint64_t *entry)
{
    /*
     * 当前还没有文件系统，用户 ELF 先作为内核镜像携带的 blob 存在。loader 仍按
     * ELF header/program header 建立用户映射；后续接文件系统时只需要替换 blob
     * 来源。
     */
    return user_elf_load(space, __user_elf_start,
                         (uint64_t)(__user_elf_end - __user_elf_start),
                         entry);
}

static void user_task_entry(void *arg)
{
    struct user_task_start *start = (struct user_task_start *)arg;

    riscv_enter_user(start->entry, start->user_stack_top,
                     start->trap_stack_top, start->arg0, start->arg1,
                     start->arg2, start->arg3);

    early_halt_forever();
}

static void user_idle_entry(void *arg)
{
    (void)arg;

    printk("User scheduler idle ready\r\n");
    idle_status_due = 1;
    timer_event_init(&idle_status_timer, idle_status_timeout, 0);
    (void)timer_schedule_ms(&idle_status_timer, USER_IDLE_STATUS_PERIOD_MS,
                            USER_IDLE_STATUS_PERIOD_MS);
    for (;;)
    {
        if (idle_status_due)
        {
            idle_status_due = 0;
            print_user_demo_status();
        }
        console_drain_input();
        __asm__ volatile("wfi" ::: "memory");
    }
}

static int create_one_user_task(uint64_t index, const char *name,
                                uint64_t startup_delay_ms,
                                uint64_t loop_delay_ms,
                                uint64_t repeat_count)
{
    void *kernel_stack_phys = phys_alloc_pages(USER_KERNEL_STACK_PAGES);
    void *user_stack_phys = phys_alloc_pages(USER_STACK_PAGES);
    void *trap_stack_phys = phys_alloc_pages(USER_TRAP_STACK_PAGES);
    uint64_t user_entry = 0;
    uint64_t kernel_stack_size = USER_KERNEL_STACK_PAGES * VM_PAGE_SIZE;
    uint64_t user_stack_size = USER_STACK_PAGES * VM_PAGE_SIZE;
    uint64_t trap_stack_size = USER_TRAP_STACK_PAGES * VM_PAGE_SIZE;
    uint64_t user_stack_base = USER_STACK_TOP - user_stack_size;

    if (!kernel_stack_phys || !user_stack_phys || !trap_stack_phys)
    {
        if (kernel_stack_phys)
        {
            phys_free_pages(kernel_stack_phys, USER_KERNEL_STACK_PAGES);
        }
        if (user_stack_phys)
        {
            phys_free_pages(user_stack_phys, USER_STACK_PAGES);
        }
        if (trap_stack_phys)
        {
            phys_free_pages(trap_stack_phys, USER_TRAP_STACK_PAGES);
        }
        printk("User task stack allocation failed\r\n");
        return 0;
    }

    if (!vm_space_create(&user_vms[index]))
    {
        printk("User page table allocation failed\r\n");
        phys_free_pages(kernel_stack_phys, USER_KERNEL_STACK_PAGES);
        phys_free_pages(user_stack_phys, USER_STACK_PAGES);
        phys_free_pages(trap_stack_phys, USER_TRAP_STACK_PAGES);
        return 0;
    }

    if (!vm_copy_kernel_mappings(&user_vms[index], kernel_vm_space()))
    {
        printk("User kernel mapping copy failed\r\n");
        phys_free_pages(kernel_stack_phys, USER_KERNEL_STACK_PAGES);
        phys_free_pages(user_stack_phys, USER_STACK_PAGES);
        phys_free_pages(trap_stack_phys, USER_TRAP_STACK_PAGES);
        return 0;
    }

    if (!load_user_image(&user_vms[index], &user_entry))
    {
        phys_free_pages(kernel_stack_phys, USER_KERNEL_STACK_PAGES);
        phys_free_pages(user_stack_phys, USER_STACK_PAGES);
        phys_free_pages(trap_stack_phys, USER_TRAP_STACK_PAGES);
        return 0;
    }

    if (!vm_map_range(&user_vms[index], user_stack_base,
                      (uint64_t)(uintptr_t)user_stack_phys,
                      user_stack_size,
                      VM_MAP_READ | VM_MAP_WRITE | VM_MAP_USER))
    {
        printk("User stack mapping failed\r\n");
        phys_free_pages(kernel_stack_phys, USER_KERNEL_STACK_PAGES);
        phys_free_pages(user_stack_phys, USER_STACK_PAGES);
        phys_free_pages(trap_stack_phys, USER_TRAP_STACK_PAGES);
        return 0;
    }

    user_starts[index].entry = user_entry;
    user_starts[index].user_stack_top = USER_STACK_TOP;
    user_starts[index].trap_stack_top =
        (uint64_t)(uintptr_t)trap_stack_phys + trap_stack_size;
    user_starts[index].arg0 = index;
    user_starts[index].arg1 = startup_delay_ms;
    user_starts[index].arg2 = loop_delay_ms;
    user_starts[index].arg3 = repeat_count;

    if (!task_create(&user_tasks[index], name, kernel_stack_phys,
                     kernel_stack_size, user_task_entry, &user_starts[index]))
    {
        printk("User task create failed\r\n");
        phys_free_pages(kernel_stack_phys, USER_KERNEL_STACK_PAGES);
        phys_free_pages(user_stack_phys, USER_STACK_PAGES);
        phys_free_pages(trap_stack_phys, USER_TRAP_STACK_PAGES);
        return 0;
    }

    user_tasks[index].vm_space = &user_vms[index];
    return 1;
}

static int create_idle_task(void)
{
    void *stack = phys_alloc_pages(USER_IDLE_STACK_PAGES);
    if (!stack)
    {
        printk("User idle stack allocation failed\r\n");
        return 0;
    }

    if (!task_create(&idle_task, "user-idle", stack,
                     USER_IDLE_STACK_PAGES * VM_PAGE_SIZE, user_idle_entry, 0))
    {
        printk("User idle task create failed\r\n");
        phys_free_pages(stack, USER_IDLE_STACK_PAGES);
        return 0;
    }

    idle_task.vm_space = kernel_vm_space();
    sched_set_idle_task(&idle_task);
    return 1;
}

int user_demo_run(void)
{
    if (!create_one_user_task(0, "user-a", 300, 1000, 0))
    {
        return 0;
    }

    if (!create_one_user_task(1, "user-b", 600, 1000, 0))
    {
        return 0;
    }

    if (!create_one_user_task(2, "user-c", 900, 1000, 8))
    {
        return 0;
    }

    if (!create_idle_task())
    {
        return 0;
    }

    vm_flush_all();

    printk("Starting U-mode demo tasks\r\n");
    sched_start_first();

    return 0;
}

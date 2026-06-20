#include "user.h"

#include <stdint.h>

#include "console.h"
#include "early_log.h"
#include "early_vm.h"
#include "page_alloc.h"
#include "printk.h"
#include "sched.h"
#include "task.h"
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

extern char __user_start[];
extern char __user_end[];
extern void user_demo_start(void);
extern char user_demo_message_a[];
extern char user_demo_message_a_end[];
extern char user_demo_message_b[];
extern char user_demo_message_b_end[];
extern char user_demo_message_c[];
extern char user_demo_message_c_end[];
extern void riscv_enter_user(uint64_t entry, uint64_t stack_top,
                             uint64_t trap_stack_top, uint64_t arg0,
                             uint64_t arg1, uint64_t arg2);

struct user_task_start
{
    uint64_t entry;
    uint64_t user_stack_top;
    uint64_t trap_stack_top;
    uint64_t arg0;
    uint64_t arg1;
    uint64_t arg2;
};

static struct task user_tasks[USER_DEMO_TASK_COUNT];
static struct vm_space user_vms[USER_DEMO_TASK_COUNT];
static struct user_task_start user_starts[USER_DEMO_TASK_COUNT];
static struct task idle_task;

static int map_user_section(struct vm_space *space)
{
    uint64_t start = (uint64_t)(uintptr_t)__user_start;
    uint64_t end = (uint64_t)(uintptr_t)__user_end;

    if (end <= start)
    {
        printk("User section is empty\r\n");
        return 0;
    }

    if (!vm_identity_map(space, start, end - start,
                         VM_MAP_READ | VM_MAP_EXEC | VM_MAP_USER))
    {
        printk("User section mapping failed\r\n");
        return 0;
    }

    return 1;
}

static void user_task_entry(void *arg)
{
    struct user_task_start *start = (struct user_task_start *)arg;

    riscv_enter_user(start->entry, start->user_stack_top,
                     start->trap_stack_top, start->arg0, start->arg1,
                     start->arg2);

    early_halt_forever();
}

static void user_idle_entry(void *arg)
{
    (void)arg;

    printk("User scheduler idle ready\r\n");
    for (;;)
    {
        console_drain_input();
        __asm__ volatile("wfi" ::: "memory");
    }
}

static uint64_t message_length(char *start, char *end)
{
    return (uint64_t)(uintptr_t)(end - start);
}

static int create_one_user_task(uint64_t index, const char *name,
                                char *message, uint64_t message_size,
                                uint64_t sleep_ms)
{
    void *kernel_stack_phys = phys_alloc_pages(USER_KERNEL_STACK_PAGES);
    void *user_stack_phys = phys_alloc_pages(USER_STACK_PAGES);
    void *trap_stack_phys = phys_alloc_pages(USER_TRAP_STACK_PAGES);
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

    if (!map_user_section(&user_vms[index]))
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

    user_starts[index].entry = (uint64_t)(uintptr_t)user_demo_start;
    user_starts[index].user_stack_top = USER_STACK_TOP;
    user_starts[index].trap_stack_top =
        (uint64_t)(uintptr_t)trap_stack_phys + trap_stack_size;
    user_starts[index].arg0 = (uint64_t)(uintptr_t)message;
    user_starts[index].arg1 = message_size;
    user_starts[index].arg2 = sleep_ms;

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
    return 1;
}

int user_demo_run(void)
{
    if (!create_one_user_task(0, "user-a", user_demo_message_a,
                              message_length(user_demo_message_a,
                                             user_demo_message_a_end),
                              15))
    {
        return 0;
    }

    if (!create_one_user_task(1, "user-b", user_demo_message_b,
                              message_length(user_demo_message_b,
                                             user_demo_message_b_end),
                              25))
    {
        return 0;
    }

    if (!create_one_user_task(2, "user-c", user_demo_message_c,
                              message_length(user_demo_message_c,
                                             user_demo_message_c_end),
                              35))
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

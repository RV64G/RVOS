#ifdef KERNEL_SELFTEST

#include "selftest.h"

#include "kmalloc.h"
#include "page_alloc.h"
#include "printk.h"
#include "riscv/sbi.h"
#include "sched.h"
#include "task.h"
#include "timer.h"
#include "vm.h"

#define SELFTEST_KMALLOC_BATCH 128ULL
#define KMALLOC_EXHAUST_OBJECT_SIZE (64ULL * 1024ULL)
#define TASK_SELFTEST_STACK_PAGES 2ULL
#define TASK_SELFTEST_ROUNDS 8ULL
#define TASK_PREEMPT_TARGET 64ULL
#define TASK_PREEMPT_TIMEOUT_MS 500ULL

static volatile uint64_t timer_selftest_count;
static volatile uint64_t task_a_runs;
static volatile uint64_t task_b_runs;
static volatile uint64_t task_sleep_before;
static volatile uint64_t task_sleep_after;
static volatile uint64_t task_preempt_a_runs;
static volatile uint64_t task_preempt_b_runs;

static int fail(const char *name)
{
    printk("Kernel selftest failed: ");
    printk(name);
    printk("\r\n");
    return 0;
}

static void fill_bytes(void *ptr, uint64_t size, uint8_t value)
{
    uint8_t *bytes = (uint8_t *)ptr;
    for (uint64_t i = 0; i < size; i++)
    {
        bytes[i] = value;
    }
}

static int bytes_are_zero(const void *ptr, uint64_t size)
{
    const uint8_t *bytes = (const uint8_t *)ptr;
    for (uint64_t i = 0; i < size; i++)
    {
        if (bytes[i] != 0)
        {
            return 0;
        }
    }
    return 1;
}

static void cpu_relax(void)
{
    __asm__ volatile("nop" ::: "memory");
}

static void wait_cycles(uint64_t cycles)
{
    uint64_t start = sbi_get_time();
    while (sbi_get_time() - start < cycles)
    {
        cpu_relax();
    }
}

static int wait_for_timer_count(uint64_t target, uint64_t timeout_ms)
{
    uint64_t timeout_cycles = timer_ms_to_cycles(timeout_ms);
    uint64_t start = sbi_get_time();

    while (timer_selftest_count < target)
    {
        if (sbi_get_time() - start > timeout_cycles)
        {
            return 0;
        }

        /*
         * timer event 已经设置了下一次 SBI deadline。这里用 wfi 等中断，避免自检在
         * 等待 timer 回调时空转烧 CPU。
         */
        __asm__ volatile("wfi" ::: "memory");
    }

    return 1;
}

static void timer_selftest_callback(struct timer_event *event, void *context)
{
    (void)event;
    (void)context;

    timer_selftest_count++;
}

static int test_trap_breakpoint(void)
{
    printk("Selftest trap breakpoint start\r\n");

    /*
     * 使用固定 32-bit ebreak 编码。trap handler 当前按 4 字节推进 sepc，如果 assembler
     * 生成 16-bit c.ebreak，自检就不能证明这条路径正确。
     */
    __asm__ volatile(".word 0x00100073" ::: "memory");

    printk("Selftest trap breakpoint done\r\n");
    return 1;
}

static int test_timer(void)
{
    printk("Selftest timer start\r\n");

    struct timer_event event;
    timer_selftest_count = 0;
    timer_event_init(&event, timer_selftest_callback, 0);

    if (!timer_schedule_ms(&event, 1, 0))
    {
        return fail("timer oneshot schedule");
    }
    if (!wait_for_timer_count(1, 100))
    {
        return fail("timer oneshot callback");
    }
    if (event.active)
    {
        return fail("timer oneshot still active");
    }
    uint64_t count_after_oneshot = timer_selftest_count;
    wait_cycles(timer_ms_to_cycles(5));
    if (timer_selftest_count != count_after_oneshot)
    {
        return fail("timer oneshot fired twice");
    }

    timer_selftest_count = 0;
    timer_event_init(&event, timer_selftest_callback, 0);

    if (!timer_schedule_ms(&event, 1, 1))
    {
        return fail("timer periodic schedule");
    }
    if (!wait_for_timer_count(3, 100))
    {
        timer_cancel(&event);
        return fail("timer periodic callback");
    }

    timer_cancel(&event);
    uint64_t count_after_cancel = timer_selftest_count;
    wait_cycles(timer_ms_to_cycles(5));
    if (timer_selftest_count != count_after_cancel)
    {
        return fail("timer callback after cancel");
    }

    printk("Selftest timer done\r\n");
    return 1;
}

static void task_selftest_entry(void *arg)
{
    volatile uint64_t *counter = (volatile uint64_t *)arg;

    for (uint64_t i = 0; i < TASK_SELFTEST_ROUNDS; i++)
    {
        (*counter)++;
        task_yield();
    }
}

static int test_task_switch(void)
{
    printk("Selftest task switch start\r\n");

    struct task boot_task;
    struct task task_a;
    struct task task_b;
    void *stack_a = phys_alloc_pages(TASK_SELFTEST_STACK_PAGES);
    void *stack_b = phys_alloc_pages(TASK_SELFTEST_STACK_PAGES);
    if (!stack_a || !stack_b)
    {
        if (stack_a)
        {
            phys_free_pages(stack_a, TASK_SELFTEST_STACK_PAGES);
        }
        if (stack_b)
        {
            phys_free_pages(stack_b, TASK_SELFTEST_STACK_PAGES);
        }
        return fail("task stack allocation");
    }

    task_a_runs = 0;
    task_b_runs = 0;
    task_system_init(&boot_task, "selftest-main");
    if (!task_create(&task_a, "selftest-a", stack_a,
                     TASK_SELFTEST_STACK_PAGES * VM_PAGE_SIZE,
                     task_selftest_entry, (void *)&task_a_runs))
    {
        task_system_reset();
        phys_free_pages(stack_a, TASK_SELFTEST_STACK_PAGES);
        phys_free_pages(stack_b, TASK_SELFTEST_STACK_PAGES);
        return fail("task create a");
    }
    if (!task_create(&task_b, "selftest-b", stack_b,
                     TASK_SELFTEST_STACK_PAGES * VM_PAGE_SIZE,
                     task_selftest_entry, (void *)&task_b_runs))
    {
        task_system_reset();
        phys_free_pages(stack_a, TASK_SELFTEST_STACK_PAGES);
        phys_free_pages(stack_b, TASK_SELFTEST_STACK_PAGES);
        return fail("task create b");
    }

    for (uint64_t guard = 0;
         (task_a.state != TASK_ZOMBIE || task_b.state != TASK_ZOMBIE) &&
         guard < TASK_SELFTEST_ROUNDS * 8;
         guard++)
    {
        task_yield();
    }

    if (task_a.state != TASK_ZOMBIE || task_b.state != TASK_ZOMBIE)
    {
        task_system_reset();
        phys_free_pages(stack_a, TASK_SELFTEST_STACK_PAGES);
        phys_free_pages(stack_b, TASK_SELFTEST_STACK_PAGES);
        return fail("task switch did not finish");
    }
    if (task_a_runs != TASK_SELFTEST_ROUNDS ||
        task_b_runs != TASK_SELFTEST_ROUNDS)
    {
        task_system_reset();
        phys_free_pages(stack_a, TASK_SELFTEST_STACK_PAGES);
        phys_free_pages(stack_b, TASK_SELFTEST_STACK_PAGES);
        return fail("task switch counters");
    }

    task_system_reset();
    phys_free_pages(stack_a, TASK_SELFTEST_STACK_PAGES);
    phys_free_pages(stack_b, TASK_SELFTEST_STACK_PAGES);

    printk("Selftest task switch done\r\n");
    return 1;
}

static void task_sleep_entry(void *arg)
{
    (void)arg;

    task_sleep_before++;
    if (!task_sleep_ms(2))
    {
        task_sleep_after = UINT64_MAX;
        return;
    }
    task_sleep_after++;
}

static int test_task_sleep(void)
{
    printk("Selftest task sleep start\r\n");

    struct task boot_task;
    struct task sleeper;
    void *stack = phys_alloc_pages(TASK_SELFTEST_STACK_PAGES);
    if (!stack)
    {
        return fail("task sleep stack allocation");
    }

    task_sleep_before = 0;
    task_sleep_after = 0;
    task_system_init(&boot_task, "selftest-sleep-main");
    if (!task_create(&sleeper, "sleep-task", stack,
                     TASK_SELFTEST_STACK_PAGES * VM_PAGE_SIZE,
                     task_sleep_entry, 0))
    {
        task_system_reset();
        phys_free_pages(stack, TASK_SELFTEST_STACK_PAGES);
        return fail("task sleep create");
    }

    task_yield();
    if (task_sleep_before != 1 || sleeper.state != TASK_BLOCKED)
    {
        task_system_reset();
        phys_free_pages(stack, TASK_SELFTEST_STACK_PAGES);
        return fail("task sleep did not block");
    }

    uint64_t timeout_cycles = timer_ms_to_cycles(100);
    uint64_t start = sbi_get_time();
    while (sleeper.state != TASK_ZOMBIE)
    {
        if (sbi_get_time() - start > timeout_cycles)
        {
            task_system_reset();
            phys_free_pages(stack, TASK_SELFTEST_STACK_PAGES);
            return fail("task sleep timeout");
        }

        task_yield();
        if (sleeper.state != TASK_ZOMBIE)
        {
            __asm__ volatile("wfi" ::: "memory");
        }
    }

    if (task_sleep_after != 1)
    {
        task_system_reset();
        phys_free_pages(stack, TASK_SELFTEST_STACK_PAGES);
        return fail("task sleep wake counter");
    }

    task_system_reset();
    phys_free_pages(stack, TASK_SELFTEST_STACK_PAGES);

    printk("Selftest task sleep done\r\n");
    return 1;
}

static void task_preempt_tick(struct timer_event *event, void *context)
{
    (void)event;
    (void)context;

    sched_request_reschedule();
}

static void task_preempt_entry(void *arg)
{
    volatile uint64_t *counter = (volatile uint64_t *)arg;

    while (*counter < TASK_PREEMPT_TARGET)
    {
        (*counter)++;
        cpu_relax();
    }

    /*
     * 这个测试验证 timer 驱动的抢占切换，所以 task 达到目标后不能主动 yield 或返回。
     * 它留在 wfi 里，等待后续 timer interrupt 把 CPU 切给其它 task 或 selftest 主线。
     */
    for (;;)
    {
        __asm__ volatile("wfi" ::: "memory");
    }
}

static int test_task_preemption(void)
{
    printk("Selftest task preemption start\r\n");

    struct task boot_task;
    struct task task_a;
    struct task task_b;
    struct timer_event tick;
    void *stack_a = phys_alloc_pages(TASK_SELFTEST_STACK_PAGES);
    void *stack_b = phys_alloc_pages(TASK_SELFTEST_STACK_PAGES);
    if (!stack_a || !stack_b)
    {
        if (stack_a)
        {
            phys_free_pages(stack_a, TASK_SELFTEST_STACK_PAGES);
        }
        if (stack_b)
        {
            phys_free_pages(stack_b, TASK_SELFTEST_STACK_PAGES);
        }
        return fail("task preempt stack allocation");
    }

    task_preempt_a_runs = 0;
    task_preempt_b_runs = 0;
    timer_event_init(&tick, task_preempt_tick, 0);
    task_system_init(&boot_task, "selftest-preempt-main");
    if (!task_create(&task_a, "preempt-a", stack_a,
                     TASK_SELFTEST_STACK_PAGES * VM_PAGE_SIZE,
                     task_preempt_entry, (void *)&task_preempt_a_runs))
    {
        task_system_reset();
        phys_free_pages(stack_a, TASK_SELFTEST_STACK_PAGES);
        phys_free_pages(stack_b, TASK_SELFTEST_STACK_PAGES);
        return fail("task preempt create a");
    }
    if (!task_create(&task_b, "preempt-b", stack_b,
                     TASK_SELFTEST_STACK_PAGES * VM_PAGE_SIZE,
                     task_preempt_entry, (void *)&task_preempt_b_runs))
    {
        task_system_reset();
        phys_free_pages(stack_a, TASK_SELFTEST_STACK_PAGES);
        phys_free_pages(stack_b, TASK_SELFTEST_STACK_PAGES);
        return fail("task preempt create b");
    }

    if (!timer_schedule_ms(&tick, 1, 1))
    {
        task_system_reset();
        phys_free_pages(stack_a, TASK_SELFTEST_STACK_PAGES);
        phys_free_pages(stack_b, TASK_SELFTEST_STACK_PAGES);
        return fail("task preempt timer schedule");
    }

    uint64_t timeout_cycles = timer_ms_to_cycles(TASK_PREEMPT_TIMEOUT_MS);
    uint64_t start = sbi_get_time();
    while (task_preempt_a_runs < TASK_PREEMPT_TARGET ||
           task_preempt_b_runs < TASK_PREEMPT_TARGET)
    {
        if (sbi_get_time() - start > timeout_cycles)
        {
            timer_cancel(&tick);
            task_system_reset();
            phys_free_pages(stack_a, TASK_SELFTEST_STACK_PAGES);
            phys_free_pages(stack_b, TASK_SELFTEST_STACK_PAGES);
            return fail("task preempt timeout");
        }
        __asm__ volatile("wfi" ::: "memory");
    }

    timer_cancel(&tick);
    task_system_reset();
    phys_free_pages(stack_a, TASK_SELFTEST_STACK_PAGES);
    phys_free_pages(stack_b, TASK_SELFTEST_STACK_PAGES);

    printk("Selftest task preemption done\r\n");
    return 1;
}

static int test_page_allocator(void)
{
    printk("Selftest page_alloc start\r\n");

    uint64_t before = page_available_pages();
    if (before < 16)
    {
        return fail("page_alloc has too few free pages");
    }

    void *run = phys_alloc_pages(4);
    if (!run)
    {
        return fail("page_alloc 4-page run");
    }
    if (page_available_pages() != before - 4)
    {
        return fail("page_alloc count after run allocation");
    }
    for (uint64_t i = 0; i < 4; i++)
    {
        void *page = (void *)((uintptr_t)run + i * VM_PAGE_SIZE);
        if (phys_page_state(page) != PHYS_PAGE_ALLOCATED)
        {
            return fail("page_alloc allocated state");
        }
    }
    phys_free_pages(run, 4);
    if (page_available_pages() != before)
    {
        return fail("page_alloc count after run free");
    }

    phys_free_pages(run, 4);
    if (page_available_pages() != before)
    {
        return fail("page_alloc double free changed count");
    }

    /*
     * 真正把页分配器压到耗尽：记录每次分到的页，直到 phys_alloc_pages(1)
     * 返回失败。这个测试会覆盖 next-fit 扫描到内存尾部、free_pages 归零、失败
     * 返回路径，以及大批量释放后的计数恢复。
     *
     * 为了不让 kmalloc 额外吃页、干扰计数，测试直接把“下一页”的指针写进刚分到
     * 的页开头，把所有测试页串成单链表。页属于测试代码，写入它们是安全的。
     */
    void *page_list = 0;
    uint64_t allocated = 0;
    for (;;)
    {
        void *page = phys_alloc_pages(1);
        if (!page)
        {
            break;
        }
        if (phys_page_state(page) != PHYS_PAGE_ALLOCATED)
        {
            return fail("page_alloc exhaustion state");
        }
        *(void **)page = page_list;
        page_list = page;
        allocated++;
    }

    if (allocated != before)
    {
        return fail("page_alloc exhaustion count");
    }
    if (page_available_pages() != 0)
    {
        return fail("page_alloc exhaustion free count");
    }
    if (phys_alloc_pages(1) != 0)
    {
        return fail("page_alloc allocation after exhaustion");
    }

    while (page_list)
    {
        void *page = page_list;
        page_list = *(void **)page_list;
        phys_free_pages(page, 1);
    }
    if (page_available_pages() != before)
    {
        return fail("page_alloc count after exhaustion free");
    }

    printk("Selftest page_alloc done\r\n");
    return 1;
}

static int test_vm(void)
{
    printk("Selftest vm start\r\n");

    struct vm_space space;
    if (!vm_space_create(&space))
    {
        return fail("vm_space_create");
    }

    void *phys = phys_alloc_pages(4);
    if (!phys)
    {
        return fail("vm test backing pages");
    }

    uint64_t pa = (uint64_t)(uintptr_t)phys;
    uint64_t va = 0x40000000ULL;
    if (!vm_map_range(&space, va, pa, VM_PAGE_SIZE * 4,
                      VM_MAP_READ | VM_MAP_WRITE))
    {
        return fail("vm_map_range basic");
    }

    struct vm_mapping mapping;
    if (!vm_query(&space, va + 123, &mapping))
    {
        return fail("vm_query mapped address");
    }
    if (mapping.pa != pa + 123 || mapping.leaf_size != VM_PAGE_SIZE ||
        mapping.flags != (VM_MAP_READ | VM_MAP_WRITE))
    {
        return fail("vm_query mapped details");
    }

    if (!vm_unmap_range(&space, va, VM_PAGE_SIZE * 4))
    {
        return fail("vm_unmap_range basic");
    }
    if (vm_query(&space, va, &mapping))
    {
        return fail("vm_query after unmap");
    }

    uint64_t rollback_va = 0x50000000ULL;
    uint64_t conflict_va = rollback_va + VM_PAGE_SIZE;
    if (!vm_map_range(&space, conflict_va, pa + VM_PAGE_SIZE * 3,
                      VM_PAGE_SIZE, VM_MAP_READ))
    {
        return fail("vm rollback setup");
    }

    uint64_t mapped_pages_before = space.mapped_pages;
    if (vm_map_range(&space, rollback_va, pa, VM_PAGE_SIZE * 3, VM_MAP_READ))
    {
        return fail("vm rollback conflict was accepted");
    }
    if (vm_query(&space, rollback_va, &mapping))
    {
        return fail("vm rollback left prefix mapping");
    }
    if (!vm_query(&space, conflict_va, &mapping))
    {
        return fail("vm rollback removed old mapping");
    }
    if (space.mapped_pages != mapped_pages_before)
    {
        return fail("vm rollback mapped_pages");
    }
    if (!vm_unmap_range(&space, conflict_va, VM_PAGE_SIZE))
    {
        return fail("vm rollback cleanup");
    }

    phys_free_pages(phys, 4);

    printk("Selftest vm done\r\n");
    return 1;
}

static int test_kmalloc(void)
{
    printk("Selftest kmalloc start\r\n");

    void *zeroed = kzalloc(257);
    if (!zeroed)
    {
        return fail("kzalloc allocation");
    }
    if (!bytes_are_zero(zeroed, 257))
    {
        return fail("kzalloc zero fill");
    }
    fill_bytes(zeroed, 257, 0xa5);
    kfree(zeroed);

    void *big = kmalloc(VM_PAGE_SIZE + 512);
    if (!big)
    {
        return fail("kmalloc big allocation");
    }
    fill_bytes(big, VM_PAGE_SIZE + 512, 0x5a);
    kfree(big);

    void *ptrs[SELFTEST_KMALLOC_BATCH];
    for (uint64_t i = 0; i < SELFTEST_KMALLOC_BATCH; i++)
    {
        uint64_t size = 1 + ((i * 37) % 1024);
        ptrs[i] = kmalloc(size);
        if (!ptrs[i])
        {
            return fail("kmalloc batch allocation");
        }
        fill_bytes(ptrs[i], size, (uint8_t)i);
    }

    for (uint64_t i = 0; i < SELFTEST_KMALLOC_BATCH; i += 2)
    {
        kfree(ptrs[i]);
        ptrs[i] = 0;
    }
    for (uint64_t i = 0; i < SELFTEST_KMALLOC_BATCH; i += 2)
    {
        ptrs[i] = kmalloc(64 + i);
        if (!ptrs[i])
        {
            return fail("kmalloc reuse allocation");
        }
    }
    for (uint64_t i = SELFTEST_KMALLOC_BATCH; i > 0; i--)
    {
        kfree(ptrs[i - 1]);
    }

    printk("Selftest kmalloc done\r\n");
    return 1;
}

static int test_kmalloc_exhaustion(void)
{
    printk("Selftest kmalloc exhaustion start\r\n");

    /*
     * 当前 kmalloc 堆只增长、不收缩：grow_heap() 从 page allocator 拿到的页即使
     * 后续通过 kfree() 回到 kmalloc 空闲链表，也不会再归还给 phys_free_pages()。
     *
     * 因此这个测试必须放在 selftest 最后一项。它会反复申请较大的对象，直到
     * kmalloc 无法再从 page allocator 扩张并返回 0；随后把对象还给 kmalloc，
     * 验证 kfree 链表路径不会崩，但不要求 page_available_pages() 恢复到测试前。
     *
     * 对象大小故意选 64KB，而不是 4KB。这样仍然会把 kmalloc/page allocator 压到
     * 耗尽，但循环次数少得多，更适合放进 CI 的 make test。
     */
    void *object_list = 0;
    uint64_t allocated = 0;

    for (;;)
    {
        void *object = kmalloc(KMALLOC_EXHAUST_OBJECT_SIZE);
        if (!object)
        {
            break;
        }

        *(void **)object = object_list;
        object_list = object;
        allocated++;
    }

    if (allocated == 0)
    {
        return fail("kmalloc exhaustion allocated no objects");
    }
    if (kmalloc(KMALLOC_EXHAUST_OBJECT_SIZE) != 0)
    {
        return fail("kmalloc allocation after exhaustion");
    }

    while (object_list)
    {
        void *object = object_list;
        object_list = *(void **)object_list;
        kfree(object);
    }

    printk("Selftest kmalloc exhaustion done\r\n");
    return 1;
}

int kernel_selftest_run(void)
{
    printk("Kernel selftest start\r\n");

    if (!test_trap_breakpoint())
    {
        return 0;
    }
    if (!test_timer())
    {
        return 0;
    }
    if (!test_task_switch())
    {
        return 0;
    }
    if (!test_task_sleep())
    {
        return 0;
    }
    if (!test_task_preemption())
    {
        return 0;
    }
    if (!test_page_allocator())
    {
        return 0;
    }
    if (!test_vm())
    {
        return 0;
    }
    if (!test_kmalloc())
    {
        return 0;
    }
    if (!test_kmalloc_exhaustion())
    {
        return 0;
    }

    return 1;
}

#endif

#include "kernel.h"
#include "fdt.h"
#include "syscalls.h"
#include "kernel/boot_info.h"
#include "kernel/hart.h"
#include "kernel/mm.h"
#include "arch/sbi.h"

#ifdef RUN_TEST
#include "../test/test.h"
#endif

/*
 * Following functions SHOULD be called ONLY ONE time here,
 * so just declared here ONCE and NOT included in file os.h.
 */
// extern void uart_init(void);  // Not needed when using SBI console
extern void os_main(void); // TODO: should be in a proper header file
extern struct context *current_ctx;

/**
 * @brief 内核的 C 语言入口函数
 * @details
 *   此函数由 `arch/riscv/start.S` 中的汇编代码在完成基本的硬件初始化后调用。
 *   它的核心职责是初始化内核的各个关键子系统，创建初始任务，并启动调度器。
 *
 * @callgraph
 *   - `_start` (arch/riscv/start.S)
 *     - 设置栈指针
 *     - 清理 BSS 段
 *     - 跳转到 `start_kernel`
 *   - `start_kernel` (self)
 *     - `page_init()`: 初始化页表和内存管理
 *     - `trap_init()`: 设置陷阱向量表
 *     - `plic_init()`: 初始化平台级中断控制器
 *     - `timer_init()`: 初始化时钟中断
 *     - `sched_init()`: 初始化调度器和任务数组
 *     - `os_main()`: 创建用户态的初始任务
 *     - `kernel_scheduler()`: 启动内核调度循环，永不返回
 */
void start_kernel(void)
{
    /* Initialize boot information from device tree */
    // boot_info_init();

    /* Initialize hardware info from device tree using Rust */
    // init_fdt_and_devices_rust(get_dtb_addr());

    /* Use SBI console instead of direct UART manipulation */
    printk("Hello, RVOS!\n");

    /* Display hart information */
    long current_hartid = sbi_get_hartid();
    printk("RVOS: Starting kernel on Hart %ld\n", current_hartid);

    /* Display all hart status using Hart management */
    hart_print_status_all();

    page_init();

    malloc_init();

    trap_init();

    // verify_syscall_table(); // 初次验证系统调用表

    plic_init();

    timer_init();

    sched_init();

#ifdef RUN_TEST
    test_main();
#else
    os_main();
#endif

    // disable_pmp(); // 禁用PMP，允许U-Mode访问所有内存

    kernel_scheduler();

    printk("Would not go here!\n");
    while (1)
    {
    }; // stop here!
}

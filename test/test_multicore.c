#include "kernel.h"
#include "kernel/hart.h"
#include "kernel/printk.h"
#include "riscv/sbi.h"
#include "uapi/printf.h" // 假设用户态的 printf 在这里声明

// 声明从汇编启动的函数
extern void _secondary_start(void);

/**
 * @brief 将在用户态运行的任务函数
 * @param hartid 这个参数由 _secondary_start 通过 a0 寄存器传入
 */
void user_task_test(long hartid)
{
    // 无限循环打印
    while (1)
    {
        // 直接调用用户态的 printf 函数
        printf("Hart %ld is running in User Mode!\n", hartid);

        // 加入一个简单的、与hartid相关的延迟，让输出不那么整齐
        for (volatile int i = 0; i < 200000 * (hartid + 1) + 600000; i++)
            ;
    }
}

/**
 * @brief 内核态的主测试函数 (由 hart 1 执行)
 */
void test_user_multicore_start(void)
{
    long boot_hart_id = sbi_get_hartid();
    if (boot_hart_id != 1)
    {
        // 如果当前核心不是 hart 1，就地待机
        while (1)
            asm volatile("wfi");
    }

    printk("Kernel: Boot hart (ID: %ld) is starting harts 2 and 3 in User Mode...\n", boot_hart_id);

    // 初始化所有核心的 Per-CPU 数据
    for (int i = 0; i < MAXNUM_CPU; i++)
    {
        cpu_data_area[i].hart_id = i;
    }

    // 启动 hart 2 和 3
    // 第三个参数(opaque)我们将要执行的用户态函数 user_task 的地址传过去
    hart_start(2, (unsigned long)_secondary_start, (unsigned long)user_task_test);
    hart_start(3, (unsigned long)_secondary_start, (unsigned long)user_task_test);

    printk("Kernel: Hart %ld has requested harts 2 and 3 to start. Now entering WFI.\n", boot_hart_id);

    // 主核心完成任务，进入低功耗等待状态
    while (1)
    {
        asm volatile("wfi");
    }
}

/**
 * @file hart.c
 * @brief Hart状态管理模块
 * @details 
 *   此模块提供了多Hart管理的高级接口，包括Hart的启动、停止、状态查询等功能。
 *   基于RISC-V SBI的Hart State Management (HSM) 扩展实现。
 * 
 * @author RVOS Team
 * @date 2025
 */

#include "kernel.h"
#include "riscv/sbi.h"
#include "kernel/printk.h"
#include "kernel/hart.h"

// 全局的 per-CPU 数据区定义
// 使用 __attribute__((used)) 防止编译器优化掉未在C代码中显式使用的全局变量
// 汇编代码 (start.S) 会直接通过名字引用这个数组
struct per_cpu_data cpu_data_area[MAXNUM_CPU] __attribute__((aligned(16), used));

/**
 * @brief 获取指定Hart的状态
 * @param hartid 要查询的Hart ID
 * @return Hart状态值，或负数表示错误
 * @note 内部函数，直接返回SBI调用的结果
 */
long do_hart_get_status(unsigned long hartid)
{
    if (hartid >= MAXNUM_CPU) {
        printk("Hart: Invalid Hart ID %lu (max: %d)\n", hartid, MAXNUM_CPU - 1);
        return SBI_ERR_INVALID_PARAM;
    }
    
    struct sbiret ret = sbi_hart_get_status(hartid);
    if (ret.error != SBI_SUCCESS) {
        return ret.error;
    }
    return ret.value;
}

/**
 * @brief 启动指定的Hart并等待其进入运行状态
 * @param hartid 要启动的Hart ID
 * @param start_addr 启动地址
 * @param opaque 传递给目标Hart的参数
 * @return 0 成功，负数表示错误码
 */
int hart_start(unsigned long hartid, unsigned long start_addr, unsigned long opaque)
{
    // 检查Hart ID是否有效
    if (hartid >= MAXNUM_CPU) {
        printk("Hart: Invalid Hart ID %lu (max: %d)\n", hartid, MAXNUM_CPU - 1);
        return -1;
    }
    
    // 检查目标Hart是否已经在运行
    long status = do_hart_get_status(hartid);
    if (status < 0) {
        printk("Hart: Failed to get status for Hart %lu, error: %ld\n", hartid, status);
        return -1;
    }
    if (status == SBI_HSM_STATE_STARTED) {
        printk("Hart: Hart %lu is already running\n", hartid);
        return 0;  // 已经启动，返回成功
    }
    if (status != SBI_HSM_STATE_STOPPED) {
        printk("Hart: Hart %lu is not in STOPPED state (current: %ld), cannot start.\n", hartid, status);
        return -1;
    }
    
    printk("Hart: Starting Hart %lu at address 0x%lx\n", hartid, start_addr);
    
    // 启动目标Hart
    struct sbiret ret = sbi_hart_start(hartid, start_addr, opaque);
    if (ret.error != SBI_SUCCESS) {
        printk("Hart: Failed to start Hart %lu, error code: %ld\n", hartid, ret.error);
        return (int)ret.error;
    }
    
    // 等待Hart启动完成
    int timeout = 1000000;  // 简单的超时计数器
    while (timeout-- > 0) {
        status = do_hart_get_status(hartid);
        if (status == SBI_HSM_STATE_STARTED) {
            printk("Hart: Hart %lu started successfully\n", hartid);
            return 0;
        }
        
        // 简单的延时
        for (volatile int i = 0; i < 100; i++);
    }
    
    printk("Hart: Timeout waiting for Hart %lu to start\n", hartid);
    return -2;  // 超时错误
}

/**
 * @brief 停止当前Hart
 * @details 此函数调用后当前Hart将停止运行，函数不应返回
 * @return 如果返回则表示停止失败
 */
int hart_stop_self(void)
{
    long hartid = sbi_get_hartid();
    
    printk("Hart: Stopping current Hart %ld\n", hartid);
    
    // 调用SBI HSM停止当前Hart
    struct sbiret ret = sbi_hart_stop();
    
    // 如果我们到达这里，说明SBI HSM停止失败
    printk("Hart: Failed to stop Hart %ld, error code: %ld\n", hartid, ret.error);
    printk("Hart: Falling back to WFI loop\n");
    
    // 回退到WFI循环
    while (1) {
        asm volatile("wfi");
    }
    
    return (int)ret.error;  // 理论上不会到达这里
}

/**
 * @brief 获取Hart状态的文本描述
 * @param status Hart状态值
 * @return 状态描述字符串
 */
static const char* hart_get_status_name(long status)
{
    switch (status) {
        case SBI_HSM_STATE_STARTED:
            return "RUNNING";
        case SBI_HSM_STATE_STOPPED:
            return "STOPPED";
        case SBI_HSM_STATE_START_PENDING:
            return "STARTING";
        case SBI_HSM_STATE_STOP_PENDING:
            return "STOPPING";
        case SBI_HSM_STATE_SUSPENDED:
            return "SUSPENDED";
        case SBI_HSM_STATE_SUSPEND_PENDING:
            return "SUSPENDING";
        case SBI_HSM_STATE_RESUME_PENDING:
            return "RESUMING";
        default:
            return "UNKNOWN/ERROR";
    }
}

/**
 * @brief 打印所有Hart的状态信息
 * @details 调试函数，用于显示系统中所有Hart的当前状态
 */
void hart_print_status_all(void)
{
    printk("Hart: Status Summary:\n");
    printk("Hart: ----------------------------------------\n");
    
    for (unsigned long i = 0; i < MAXNUM_CPU; i++) {
        long status = do_hart_get_status(i);
        if (status >= 0) {
            printk("Hart: Hart %lu: %s\n", i, hart_get_status_name(status));
        } else {
            printk("Hart: Hart %lu: ERROR (%ld)\n", i, status);
        }
    }
    
    printk("Hart: ----------------------------------------\n");
}

/**
 * @brief 挂起当前Hart
 * @param suspend_type 挂起类型
 * @param resume_addr 恢复地址
 * @param opaque 传递给恢复函数的参数
 * @return 0 成功，负数表示错误码
 */
int hart_suspend_self(unsigned long suspend_type, unsigned long resume_addr, unsigned long opaque)
{
    long hartid = sbi_get_hartid();
    
    printk("Hart: Suspending Hart %ld (type: %lu, resume at: 0x%lx)\n", 
           hartid, suspend_type, resume_addr);
    
    struct sbiret ret = sbi_hart_suspend(suspend_type, resume_addr, opaque);
    
    if (ret.error != SBI_SUCCESS) {
        printk("Hart: Failed to suspend Hart %ld, error code: %ld\n", hartid, ret.error);
        return (int)ret.error;
    }
    
    // 如果挂起成功，当Hart恢复时会从这里继续执行
    printk("Hart: Hart %ld resumed from suspend\n", hartid);
    return 0;
}

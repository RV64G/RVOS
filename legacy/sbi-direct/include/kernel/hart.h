/**
 * @file hart.h
 * @brief Hart状态管理模块头文件
 * @details 
 *   此模块提供了多Hart管理的高级接口，包括Hart的启动、停止、状态查询等功能。
 *   基于RISC-V SBI的Hart State Management (HSM) 扩展实现。
 * 
 * @author RVOS Team
 * @date 2025
 */

#ifndef __HART_H__
#define __HART_H__

#include "kernel/types.h"

// 每个核心的私有数据结构
// 使用 __attribute__((aligned(16))) 确保对齐
struct per_cpu_data {
    long hart_id;
    // 未来可以添加:
    // struct task *current_task;
    // int scheduler_ticks;
};

// 全局的 per-CPU 数据区，汇编代码会通过名字来引用它
extern struct per_cpu_data cpu_data_area[MAXNUM_CPU];

/**
 * @brief 获取当前核心的per_cpu_data结构体指针
 * @return 指向当前核心per_cpu_data的指针
 */
static inline struct per_cpu_data* get_cpu_data(void) {
    struct per_cpu_data *ptr;
    // 从 tp 寄存器读取指针
    asm volatile("mv %0, tp" : "=r"(ptr));
    return ptr;
}


/**
 * @brief 启动指定的Hart并等待其进入运行状态
 * @param hartid 要启动的Hart ID
 * @param start_addr 启动地址
 * @param opaque 传递给目标Hart的参数
 * @return 0 成功，负数表示错误码
 */
int hart_start(unsigned long hartid, unsigned long start_addr, unsigned long opaque);

/**
 * @brief 停止当前Hart
 * @details 此函数调用后当前Hart将停止运行，函数不应返回
 * @return 如果返回则表示停止失败
 */
int hart_stop_self(void);

/**
 * @brief 打印所有Hart的状态信息
 * @details 调试函数，用于显示系统中所有Hart的当前状态
 */
void hart_print_status_all(void);

/**
 * @brief 挂起当前Hart
 * @param suspend_type 挂起类型
 * @param resume_addr 恢复地址
 * @param opaque 传递给恢复函数的参数
 * @return 0 成功，负数表示错误码
 */
int hart_suspend_self(unsigned long suspend_type, unsigned long resume_addr, unsigned long opaque);

#endif /* __HART_H__ */

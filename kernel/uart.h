#ifndef KERNEL_UART_H
#define KERNEL_UART_H

#include <stdint.h>

/**
 * 初始化轮询输出用的串口后端。
 *
 * 当前按 ns16550/16550a 兼容 UART 处理。DTB 解析阶段已经把 MMIO 基址归一化到
 * platform_info，调用侧只需要把 base 传进来。
 */
int uart_console_init(uint64_t base, uint64_t size);

/**
 * 返回 UART 后端是否已经可用。
 */
int uart_ready(void);

/**
 * 通过 UART 输出一个字符。
 *
 * 调用前应保证 uart_ready() 为真；函数会等待发送保持寄存器变空。
 */
void uart_putchar(int ch);

#endif

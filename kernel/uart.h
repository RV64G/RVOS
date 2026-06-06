#ifndef KERNEL_UART_H
#define KERNEL_UART_H

#include <stdint.h>

/**
 * 初始化轮询输出用的串口后端。
 *
 * 当前按 ns16550/16550a 兼容 UART 处理。reg_shift/reg_io_width 来自 DTB：
 * reg_shift 决定寄存器编号到字节偏移的换算，reg_io_width 决定 MMIO 访问宽度。
 */
int uart_console_init(uint64_t base, uint64_t size, uint32_t reg_shift,
                      uint32_t reg_io_width);

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

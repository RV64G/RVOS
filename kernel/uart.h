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

/**
 * 尝试通过 UART 异步输出一段缓冲区。
 *
 * 数据会先进入内核 TX ring buffer，再由 UART THR-empty 中断继续发送。函数返回 1
 * 表示缓冲区内容已经全部交给 UART 后端；返回 0 表示 UART 还不可用，调用者应退回
 * SBI 等早期输出路径。
 */
int uart_write(const char *s, uint64_t len);

/**
 * 开启 ns16550 中断输出/输入。
 */
void uart_enable_interrupts(void);

/**
 * 处理 UART 中断。
 *
 * RX 方向把已经到达的字符读入 console input buffer；TX 方向把 TX ring buffer 中
 * 的字符继续写入 THR。
 */
void uart_handle_interrupt(void);

#endif

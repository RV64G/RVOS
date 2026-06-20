#ifndef KERNEL_CONSOLE_H
#define KERNEL_CONSOLE_H

/**
 * 初始化临时内核 console 输入缓冲区。
 */
void console_init(void);

/**
 * 由 UART RX 中断路径调用，把一个输入字符放进 console ring buffer。
 *
 * 这个函数不打印、不阻塞，也不分配内存。
 */
void console_input_putc(int ch);

/**
 * 从输入缓冲区取出已经收到的字符并回显。
 *
 * 当前只是调试入口，用来证明 UART 中断输入链路已经打通。
 */
void console_drain_input(void);

/**
 * 临时 console 循环：等待中断唤醒，drain 输入，再继续 wfi。
 *
 * 后续有 shell 或 read(0, ...) 后，这个函数会被正式 console task 替代。
 */
void console_debug_loop(void);

#endif

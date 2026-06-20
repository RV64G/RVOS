#ifndef KERNEL_IRQ_H
#define KERNEL_IRQ_H

/**
 * 初始化当前 hart 的外部中断控制器。
 *
 * 当前接 PLIC + UART：RX 负责输入字符入队，TX empty 负责继续发送 printk 缓冲区。
 */
int irq_init(void);

/**
 * trap 分发层在 supervisor external interrupt 时调用。
 */
void irq_handle_external(void);

#endif

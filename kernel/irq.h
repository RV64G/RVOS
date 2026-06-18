#ifndef KERNEL_IRQ_H
#define KERNEL_IRQ_H

/**
 * 初始化当前 hart 的外部中断控制器。
 *
 * 第一版只接 PLIC + UART RX，用来验证串口中断输入链路。
 */
int irq_init(void);

/**
 * trap 分发层在 supervisor external interrupt 时调用。
 */
void irq_handle_external(void);

#endif

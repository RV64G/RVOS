#include "uart.h"

#include <stdint.h>

/*
 * QEMU virt 和当前开发板 DTB 中暴露的是 ns16550 兼容 UART。这里先只实现最小发送
 * 路径：等待 LSR.THRE 置位后写 THR。波特率、FIFO、中断模式暂时交给固件留下的
 * 默认状态；等驱动层成形后再补完整初始化。
 */
#define UART_THR 0U
#define UART_LSR 5U
#define UART_LSR_THRE (1U << 5)

static volatile uint8_t *uart_regs;
static uint64_t uart_reg_size;

static uint8_t uart_read(uint32_t offset)
{
    return uart_regs[offset];
}

static void uart_write(uint32_t offset, uint8_t value)
{
    uart_regs[offset] = value;
}

int uart_console_init(uint64_t base, uint64_t size)
{
    /*
     * 最小发送路径至少需要 THR 和 LSR 两个寄存器。这里不猜测硬件，如果 DTB 给出的
     * range 不覆盖 LSR，就让 printk 保持 SBI fallback。
     */
    if (base == 0 || size <= UART_LSR) {
        return 0;
    }

    uart_regs = (volatile uint8_t *)(uintptr_t)base;
    uart_reg_size = size;
    return 1;
}

int uart_ready(void)
{
    return uart_regs != 0 && uart_reg_size > UART_LSR;
}

void uart_putchar(int ch)
{
    while ((uart_read(UART_LSR) & UART_LSR_THRE) == 0) {
        __asm__ volatile ("nop");
    }

    uart_write(UART_THR, (uint8_t)ch);
}

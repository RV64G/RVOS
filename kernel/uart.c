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

typedef uint32_t (*uart_read_fn)(uintptr_t addr);
typedef void (*uart_write_fn)(uintptr_t addr, uint32_t value);

static volatile uint8_t *uart_regs;
static uint64_t uart_reg_size;
static uint32_t uart_reg_shift;
static uint32_t uart_access_width;
static uart_read_fn uart_read_reg;
static uart_write_fn uart_write_reg;

static uint32_t mmio_read8(uintptr_t addr)
{
    return *(volatile uint8_t *)addr;
}

static uint32_t mmio_read16(uintptr_t addr)
{
    return *(volatile uint16_t *)addr;
}

static uint32_t mmio_read32(uintptr_t addr)
{
    return *(volatile uint32_t *)addr;
}

static void mmio_write8(uintptr_t addr, uint32_t value)
{
    *(volatile uint8_t *)addr = (uint8_t)value;
}

static void mmio_write16(uintptr_t addr, uint32_t value)
{
    *(volatile uint16_t *)addr = (uint16_t)value;
}

static void mmio_write32(uintptr_t addr, uint32_t value)
{
    *(volatile uint32_t *)addr = value;
}

static uint64_t uart_offset(uint32_t reg)
{
    return (uint64_t)reg << uart_reg_shift;
}

static uintptr_t uart_addr(uint32_t reg)
{
    return (uintptr_t)(uart_regs + uart_offset(reg));
}

static uint32_t uart_read(uint32_t reg)
{
    return uart_read_reg(uart_addr(reg));
}

static void uart_write(uint32_t reg, uint8_t value)
{
    uart_write_reg(uart_addr(reg), value);
}

static int select_mmio_access(uint32_t reg_io_width, uart_read_fn *read,
                              uart_write_fn *write)
{
    switch (reg_io_width) {
    case 1:
        *read = mmio_read8;
        *write = mmio_write8;
        break;
    case 2:
        *read = mmio_read16;
        *write = mmio_write16;
        break;
    case 4:
        *read = mmio_read32;
        *write = mmio_write32;
        break;
    default:
        return 0;
    }

    return 1;
}

int uart_console_init(uint64_t base, uint64_t size, uint32_t reg_shift,
                      uint32_t reg_io_width)
{
    /*
     * 最小发送路径至少需要 THR 和 LSR 两个寄存器。LSR 的实际字节偏移需要考虑
     * reg-shift：reg-shift=2 时，寄存器 5 在 base + 20。
     */
    if (base == 0 || reg_shift >= 32) {
        return 0;
    }

    uart_read_fn read = 0;
    uart_write_fn write = 0;
    if (!select_mmio_access(reg_io_width, &read, &write)) {
        return 0;
    }

    uint64_t lsr_offset = (uint64_t)UART_LSR << reg_shift;
    if (size <= lsr_offset || uart_access_width > size - lsr_offset) {
        return 0;
    }

    uart_regs = (volatile uint8_t *)(uintptr_t)base;
    uart_reg_size = size;
    uart_reg_shift = reg_shift;
    uart_access_width = reg_io_width;
    uart_read_reg = read;
    uart_write_reg = write;
    return 1;
}

int uart_ready(void)
{
    if (uart_regs == 0) {
        return 0;
    }

    uint64_t lsr_offset = uart_offset(UART_LSR);
    return uart_reg_size > lsr_offset &&
           uart_access_width <= uart_reg_size - lsr_offset;
}

void uart_putchar(int ch)
{
    while ((uart_read(UART_LSR) & UART_LSR_THRE) == 0) {
        __asm__ volatile("nop");
    }

    uart_write(UART_THR, (uint8_t)ch);
}

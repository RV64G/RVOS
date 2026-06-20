#include "uart.h"

#include <stdint.h>

#include "console.h"
#include "spinlock.h"

/*
 * QEMU virt 和当前开发板 DTB 中暴露的是 ns16550 兼容 UART。早期 printk 仍可走
 * uart_putchar() 轮询输出；PLIC/UART 中断打开后，正式 printk 会进入 TX ring buffer，
 * 再由 THR-empty interrupt 继续发送。
 */
#define UART_RBR 0U
#define UART_THR 0U
#define UART_IER 1U
#define UART_LSR 5U
#define UART_IER_RDI (1U << 0)
#define UART_IER_THRI (1U << 1)
#define UART_LSR_DR (1U << 0)
#define UART_LSR_THRE (1U << 5)

#define UART_TX_CAPACITY 4096U

typedef uint32_t (*uart_read_fn)(uintptr_t addr);
typedef void (*uart_write_fn)(uintptr_t addr, uint32_t value);

static volatile uint8_t *uart_regs;
static uint64_t uart_reg_size;
static uint32_t uart_reg_shift;
static uint32_t uart_access_width;
static uart_read_fn uart_read_reg;
static uart_write_fn uart_write_reg;
static struct spinlock tx_lock;
static char tx_buffer[UART_TX_CAPACITY];
static volatile uint32_t tx_head;
static volatile uint32_t tx_tail;
static int uart_interrupts_enabled;

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

static void uart_write_register(uint32_t reg, uint8_t value)
{
    uart_write_reg(uart_addr(reg), value);
}

static uint32_t tx_next(uint32_t index)
{
    return (index + 1U) % UART_TX_CAPACITY;
}

static int tx_empty(void)
{
    return tx_head == tx_tail;
}

static int tx_full(void)
{
    return tx_next(tx_head) == tx_tail;
}

static void uart_set_tx_interrupt_locked(int enable)
{
    uint32_t ier = uart_read(UART_IER);

    /*
     * IER.THRI 控制“发送保持寄存器为空”中断。TX ring 非空时打开它，让 UART 在下一次
     * 可以写 THR 时打断内核；TX ring 为空时关闭它，否则空 THR 会持续制造无意义中断。
     *
     * 调用者必须已经持有 tx_lock，避免 printk 入队和 UART 中断 handler 同时改 IER。
     */
    if (enable && uart_interrupts_enabled)
    {
        ier |= UART_IER_THRI;
    }
    else
    {
        ier &= ~UART_IER_THRI;
    }

    uart_write_register(UART_IER, (uint8_t)ier);
}

static int uart_tx_pump_locked(void)
{
    int wrote = 0;

    /*
     * LSR.THRE 表示发送保持寄存器当前可以接收下一个字符。这里每写 1 字节后都重新
     * 读取 LSR，而不是假定 UART FIFO 有 16 字节或其它固定深度。真实硬件如果写入后
     * 立刻清掉 THRE，循环会停止，剩余内容继续留在 TX ring buffer，等下一次
     * THR-empty 中断再发送。
     */
    while (!tx_empty() && (uart_read(UART_LSR) & UART_LSR_THRE) != 0)
    {
        uart_write_register(UART_THR, (uint8_t)tx_buffer[tx_tail]);
        tx_tail = tx_next(tx_tail);
        wrote = 1;
    }

    /*
     * 发送队列空了就关闭 THR-empty 中断，否则空发送寄存器会反复触发外部中断。
     * 队列非空时保持 THR-empty 中断打开，让硬件在下一次可写时继续唤醒内核。
     */
    uart_set_tx_interrupt_locked(!tx_empty());
    return wrote;
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
    if (size <= lsr_offset || reg_io_width > size - lsr_offset) {
        return 0;
    }

    uart_regs = (volatile uint8_t *)(uintptr_t)base;
    uart_reg_size = size;
    uart_reg_shift = reg_shift;
    uart_access_width = reg_io_width;
    uart_read_reg = read;
    uart_write_reg = write;
    spinlock_init(&tx_lock, "uart-tx");
    tx_head = 0;
    tx_tail = 0;
    uart_interrupts_enabled = 0;
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

    uart_write_register(UART_THR, (uint8_t)ch);
}

int uart_write(const char *s, uint64_t len)
{
    if (!uart_ready())
    {
        return 0;
    }

    if (!uart_interrupts_enabled)
    {
        for (uint64_t i = 0; i < len; i++)
        {
            uart_putchar((int)s[i]);
        }
        return 1;
    }

    uint64_t i = 0;
    while (i < len)
    {
        spinlock_acquire(&tx_lock);
        while (i < len && !tx_full())
        {
            tx_buffer[tx_head] = s[i];
            tx_head = tx_next(tx_head);
            i++;
        }

        (void)uart_tx_pump_locked();
        int full = tx_full();
        spinlock_release(&tx_lock);

        if (full)
        {
            /*
             * printk 目前没有返回短写，也没有睡眠队列。TX ring 满时只能短暂等待硬件
             * 前进，避免直接丢日志；正常日志远小于 4 KiB 时不会走到这里。
             */
            while ((uart_read(UART_LSR) & UART_LSR_THRE) == 0)
            {
                __asm__ volatile("nop");
            }

            spinlock_acquire(&tx_lock);
            (void)uart_tx_pump_locked();
            spinlock_release(&tx_lock);
        }
    }

    return 1;
}

void uart_enable_interrupts(void)
{
    if (!uart_ready()) {
        return;
    }

    /*
     * IER.RDI 打开“接收数据可用”中断。TX 方向不在这里直接打开 THR-empty；
     * 只有 TX buffer 非空时 uart_write()/uart_handle_interrupt() 才会打开它。
     */
    uint32_t ier = uart_read(UART_IER);
    uart_write_register(UART_IER, (uint8_t)(ier | UART_IER_RDI));
    uart_interrupts_enabled = 1;
}

void uart_handle_interrupt(void)
{
    if (!uart_ready()) {
        return;
    }

    /*
     * 一次 UART interrupt 可能代表 FIFO 里已经有多个字符。这里把当前已经到达的字符
     * 读空，但不回显、不 printk，避免硬中断路径等待 UART 发送寄存器。
     */
    while ((uart_read(UART_LSR) & UART_LSR_DR) != 0) {
        console_input_putc((int)(uart_read(UART_RBR) & 0xffU));
    }

    spinlock_acquire(&tx_lock);
    (void)uart_tx_pump_locked();
    spinlock_release(&tx_lock);
}

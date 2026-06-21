#include "irq.h"

#include <stdint.h>

#include "csr.h"
#include "platform.h"
#include "printk.h"
#include "uart.h"

#define SIE_SEIE (1ULL << 9)

/*
 * PLIC 的 DTB 节点只告诉内核“PLIC MMIO 窗口从哪里开始、有多大”。窗口内部各类
 * 寄存器的偏移是 PLIC 兼容布局的一部分，不会逐项写在设备树里：
 *
 * priority[source]            : 每个中断源一个 32-bit 优先级寄存器。
 * enable[context][word]       : 每个 hart/context 一组 enable bitset。
 * threshold/claim[context]    : 每个 context 一组阈值和 claim/complete 寄存器。
 */
#define PLIC_PRIORITY_BASE 0x000000ULL
#define PLIC_ENABLE_BASE   0x002000ULL
#define PLIC_ENABLE_STRIDE 0x000080ULL
#define PLIC_CONTEXT_BASE  0x200000ULL
#define PLIC_CONTEXT_STRIDE 0x001000ULL

static volatile uint8_t *plic_base;
static uint64_t plic_size;
static uint32_t uart_irq;
static uint64_t plic_context;

static volatile uint32_t *plic_reg(uint64_t offset)
{
    return (volatile uint32_t *)(plic_base + offset);
}

static int plic_range_valid(uint64_t offset)
{
    return plic_base != 0 && offset <= plic_size && sizeof(uint32_t) <= plic_size - offset;
}

static int plic_write(uint64_t offset, uint32_t value)
{
    if (!plic_range_valid(offset))
    {
        return 0;
    }

    *plic_reg(offset) = value;
    return 1;
}

static int plic_read(uint64_t offset, uint32_t *value)
{
    if (!plic_range_valid(offset))
    {
        return 0;
    }

    *value = *plic_reg(offset);
    return 1;
}

static int plic_enable_source(uint32_t irq)
{
    /*
     * enable 区是 bitset：source id 为 irq 时，irq / 32 选择 32-bit word，
     * irq % 32 选择这个 word 里的 bit。
     */
    uint64_t enable_offset =
        PLIC_ENABLE_BASE + plic_context * PLIC_ENABLE_STRIDE +
        (uint64_t)(irq / 32U) * sizeof(uint32_t);
    uint32_t value;

    if (!plic_read(enable_offset, &value))
    {
        return 0;
    }

    value |= 1U << (irq % 32U);
    return plic_write(enable_offset, value);
}

static uint32_t plic_claim(void)
{
    /*
     * 读取 claim 会得到当前 context 下优先级最高的 pending IRQ，并同时把这个 IRQ
     * 从 pending 状态取出。返回 0 表示当前没有可处理的外部中断。
     */
    uint64_t claim_offset =
        PLIC_CONTEXT_BASE + plic_context * PLIC_CONTEXT_STRIDE + sizeof(uint32_t);
    uint32_t irq = 0;

    (void)plic_read(claim_offset, &irq);
    return irq;
}

static void plic_complete(uint32_t irq)
{
    /*
     * handler 处理完设备后，必须把同一个 irq 写回 complete。否则 PLIC 会认为这个
     * 中断还没有服务完成，后续同源中断可能无法继续送达。
     */
    uint64_t claim_offset =
        PLIC_CONTEXT_BASE + plic_context * PLIC_CONTEXT_STRIDE + sizeof(uint32_t);

    (void)plic_write(claim_offset, irq);
}

int irq_init(void)
{
    const struct platform_info *platform = platform_info();

    if (platform->irq_base == 0 || platform->irq_size == 0 ||
        platform->uart_irq == 0 || !platform->has_irq_context)
    {
        printk("IRQ init failed: missing PLIC, UART irq or context\r\n");
        return 0;
    }

    plic_base = (volatile uint8_t *)(uintptr_t)platform->irq_base;
    plic_size = platform->irq_size;
    uart_irq = platform->uart_irq;
    plic_context = platform->irq_context;

    uint64_t priority_offset = PLIC_PRIORITY_BASE + (uint64_t)uart_irq * sizeof(uint32_t);
    uint64_t threshold_offset =
        PLIC_CONTEXT_BASE + plic_context * PLIC_CONTEXT_STRIDE;

    if (!plic_write(priority_offset, 1) ||
        !plic_enable_source(uart_irq) ||
        !plic_write(threshold_offset, 0))
    {
        printk("IRQ init failed: PLIC register outside range\r\n");
        return 0;
    }

    uart_enable_interrupts();
    csr_set_sie(SIE_SEIE);

    printk("PLIC UART input ready\r\n");
    printk_field("uart_irq", uart_irq);
    printk_field("plic_context", plic_context);
    return 1;
}

void irq_handle_external(void)
{
    for (;;)
    {
        uint32_t irq = plic_claim();
        if (irq == 0)
        {
            return;
        }

        if (irq == uart_irq)
        {
            uart_handle_interrupt();
        }
        else
        {
            printk("Unhandled external interrupt\r\n");
            printk_field("irq", irq);
        }

        plic_complete(irq);
    }
}

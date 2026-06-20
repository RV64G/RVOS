#include "printk.h"

#include "platform.h"
#include "riscv/sbi.h"
#include "uart.h"

static void printk_putchar(int ch)
{
    if (uart_ready()) {
        char c = (char)ch;
        if (uart_write(&c, 1)) {
            return;
        }
    }

    sbi_console_putchar(ch);
}

void printk_init(void)
{
    const struct platform_info *platform = platform_info();

    if (platform->uart_base != 0) {
        if (uart_console_init(platform->uart_base, platform->uart_size,
                              platform->uart_reg_shift,
                              platform->uart_reg_io_width)) {
            printk("UART console ready\r\n");
        }
    }
}

/* TODO: 异步 UART 输出会让多个 task 的 printk 内容交错；后续需要 printk 层日志锁
 * 或正式日志队列，保证一条日志记录作为整体入队。 */
void printk(const char *s)
{
    const char *end = s;
    while (*end) {
        end++;
    }

    if (uart_ready() && uart_write(s, (uint64_t)(end - s))) {
        return;
    }

    while (*s) {
        printk_putchar(*s);
        s++;
    }
}

void printk_hex64(uint64_t value)
{
    static const char hex[] = "0123456789abcdef";

    printk("0x");
    for (int shift = 60; shift >= 0; shift -= 4) {
        printk_putchar(hex[(value >> shift) & 0xf]);
    }
}

void printk_u64(uint64_t value)
{
    char digits[20];
    int count = 0;

    if (value == 0) {
        printk_putchar('0');
        return;
    }

    while (value > 0) {
        digits[count++] = (char)('0' + value % 10);
        value /= 10;
    }

    while (count > 0) {
        printk_putchar(digits[--count]);
    }
}

void printk_field(const char *name, uint64_t value)
{
    printk("  ");
    printk(name);
    printk("=");
    printk_hex64(value);
    printk("\r\n");
}

void printk_dec_field(const char *name, uint64_t value)
{
    printk("  ");
    printk(name);
    printk("=");
    printk_u64(value);
    printk("\r\n");
}

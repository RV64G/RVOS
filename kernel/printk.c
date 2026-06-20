#include "printk.h"

#include "platform.h"
#include "riscv/sbi.h"
#include "spinlock.h"
#include "uart.h"

static struct spinlock printk_lock = {
    .locked = 0,
    .saved_sstatus = 0,
    .name = "printk",
};

static void printk_putchar_unlocked(int ch)
{
    if (uart_ready()) {
        char c = (char)ch;
        if (uart_write(&c, 1)) {
            return;
        }
    }

    sbi_console_putchar(ch);
}

static void printk_write_unlocked(const char *s)
{
    const char *end = s;
    while (*end) {
        end++;
    }

    if (uart_ready() && uart_write(s, (uint64_t)(end - s))) {
        return;
    }

    while (*s) {
        printk_putchar_unlocked(*s);
        s++;
    }
}

static void printk_hex64_unlocked(uint64_t value)
{
    static const char hex[] = "0123456789abcdef";

    printk_write_unlocked("0x");
    for (int shift = 60; shift >= 0; shift -= 4) {
        printk_putchar_unlocked(hex[(value >> shift) & 0xf]);
    }
}

static void printk_u64_unlocked(uint64_t value)
{
    char digits[20];
    int count = 0;

    if (value == 0) {
        printk_putchar_unlocked('0');
        return;
    }

    while (value > 0) {
        digits[count++] = (char)('0' + value % 10);
        value /= 10;
    }

    while (count > 0) {
        printk_putchar_unlocked(digits[--count]);
    }
}

void printk_init(void)
{
    const struct platform_info *platform = platform_info();

    spinlock_init(&printk_lock, "printk");
    if (platform->uart_base != 0) {
        if (uart_console_init(platform->uart_base, platform->uart_size,
                              platform->uart_reg_shift,
                              platform->uart_reg_io_width)) {
            printk("UART console ready\r\n");
        }
    }
}

void printk(const char *s)
{
    spinlock_acquire(&printk_lock);
    printk_write_unlocked(s);
    spinlock_release(&printk_lock);
}

void printk_hex64(uint64_t value)
{
    spinlock_acquire(&printk_lock);
    printk_hex64_unlocked(value);
    spinlock_release(&printk_lock);
}

void printk_u64(uint64_t value)
{
    spinlock_acquire(&printk_lock);
    printk_u64_unlocked(value);
    spinlock_release(&printk_lock);
}

void printk_field(const char *name, uint64_t value)
{
    spinlock_acquire(&printk_lock);
    printk_write_unlocked("  ");
    printk_write_unlocked(name);
    printk_write_unlocked("=");
    printk_hex64_unlocked(value);
    printk_write_unlocked("\r\n");
    spinlock_release(&printk_lock);
}

void printk_dec_field(const char *name, uint64_t value)
{
    spinlock_acquire(&printk_lock);
    printk_write_unlocked("  ");
    printk_write_unlocked(name);
    printk_write_unlocked("=");
    printk_u64_unlocked(value);
    printk_write_unlocked("\r\n");
    spinlock_release(&printk_lock);
}

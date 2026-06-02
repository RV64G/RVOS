#ifndef RVOS_KERNEL_EFI_EARLY_LOG_H
#define RVOS_KERNEL_EFI_EARLY_LOG_H

#include <stdint.h>

#include "riscv/sbi.h"

static inline void early_puts(const char *s)
{
    sbi_console_puts(s);
}

static inline void early_print_hex64(uint64_t value)
{
    static const char hex[] = "0123456789abcdef";

    early_puts("0x");
    for (int shift = 60; shift >= 0; shift -= 4) {
        sbi_console_putchar(hex[(value >> shift) & 0xf]);
    }
}

static inline void early_print_field(const char *name, uint64_t value)
{
    early_puts("  ");
    early_puts(name);
    early_puts("=");
    early_print_hex64(value);
    early_puts("\r\n");
}

static inline void early_halt_forever(void)
{
    for (;;) {
        __asm__ volatile ("wfi");
    }
}

#endif

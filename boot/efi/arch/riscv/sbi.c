#include "riscv_sbi.h"

#define SBI_EXT_LEGACY_CONSOLE 1ULL
#define SBI_EXT_DBCN 0x4442434eULL

#define SBI_DBCN_CONSOLE_WRITE_BYTE 2ULL

struct sbiret sbi_call(
    uint64_t extension,
    uint64_t function,
    uint64_t arg0,
    uint64_t arg1,
    uint64_t arg2
)
{
    register uint64_t a0 __asm__("a0") = arg0;
    register uint64_t a1 __asm__("a1") = arg1;
    register uint64_t a2 __asm__("a2") = arg2;
    register uint64_t a6 __asm__("a6") = function;
    register uint64_t a7 __asm__("a7") = extension;

    __asm__ volatile (
        "ecall"
        : "+r"(a0), "+r"(a1)
        : "r"(a2), "r"(a6), "r"(a7)
        : "memory"
    );

    struct sbiret ret = {
        .error = (long)a0,
        .value = (long)a1,
    };
    return ret;
}

static void sbi_legacy_console_putchar(char ch)
{
    sbi_call(SBI_EXT_LEGACY_CONSOLE, 0, (uint64_t)ch, 0, 0);
}

static int sbi_debug_console_putchar(char ch)
{
    struct sbiret ret = sbi_call(
        SBI_EXT_DBCN,
        SBI_DBCN_CONSOLE_WRITE_BYTE,
        (uint64_t)ch,
        0,
        0
    );

    return ret.error == 0;
}

void sbi_console_puts(const char *s)
{
    while (*s) {
        if (!sbi_debug_console_putchar(*s)) {
            sbi_legacy_console_putchar(*s);
        }
        s++;
    }
}

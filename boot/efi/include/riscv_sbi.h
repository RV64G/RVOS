#ifndef RVOS_RISCV_SBI_H
#define RVOS_RISCV_SBI_H

#include "efi.h"

struct sbiret {
    long error;
    long value;
};

struct sbiret sbi_call(
    uint64_t extension,
    uint64_t function,
    uint64_t arg0,
    uint64_t arg1,
    uint64_t arg2
);
void sbi_console_puts(const char *s);

#endif

#include "riscv/sbi.h"
#include "rvos/boot_info.h"

void rvos_kernel_entry(struct rvos_boot_info *boot_info)
{
    (void)boot_info;

    sbi_console_puts("\r\nRVOS kernel ELF entered\r\n");

    for (;;) {
        __asm__ volatile ("wfi");
    }
}

#include "efi_boot_info.h"
#include "riscv_sbi.h"

void rvos_kernel_debug_puts(const char *s)
{
    sbi_console_puts(s);
}

void rvos_kernel_stub(struct rvos_boot_info *boot_info)
{
    (void)boot_info;

    rvos_kernel_debug_puts("\r\nRVOS kernel stub entered\r\n");

    for (;;) {
        __asm__ volatile ("wfi");
    }
}

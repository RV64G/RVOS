#ifndef RVOS_EFI_BOOT_INFO_H
#define RVOS_EFI_BOOT_INFO_H

#include "efi.h"
#include "rvos/boot_info.h"

EFI_STATUS efi_collect_boot_info(
    EFI_HANDLE image_handle,
    efi_system_table_t *st,
    struct rvos_boot_info *boot_info,
    efi_memory_map_info_t *memory_map
);
void efi_print_boot_info(efi_system_table_t *st, const struct rvos_boot_info *boot_info);

void rvos_kernel_stub(struct rvos_boot_info *boot_info);
void rvos_kernel_debug_puts(const char *s);
void rvos_jump_to_kernel_stub(void *stack_top, struct rvos_boot_info *boot_info);

#endif

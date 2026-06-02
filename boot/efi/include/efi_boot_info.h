#ifndef EFI_BOOT_INFO_H
#define EFI_BOOT_INFO_H

#include "efi.h"
#include "kernel_boot_info.h"

EFI_STATUS efi_collect_boot_info(
    EFI_HANDLE image_handle,
    efi_system_table_t *st,
    struct kernel_boot_info *boot_info,
    efi_memory_map_info_t *memory_map
);
void efi_print_boot_info(efi_system_table_t *st, const struct kernel_boot_info *boot_info);
EFI_STATUS efi_load_kernel_elf(
    EFI_HANDLE image_handle,
    efi_system_table_t *st,
    uint64_t *entry,
    uint64_t *load_start,
    uint64_t *load_size
);

void jump_to_kernel(
    void *entry,
    void *stack_top,
    struct kernel_boot_info *boot_info
);

#endif

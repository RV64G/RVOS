#include "efi_boot_info.h"

/*
 * RISC-V EFI 镜像必须带一个 .reloc 数据目录。当前程序没有真正需要重定位的
 * 项目，但一些固件/加载器会拒绝完全没有 .reloc 的 PE/COFF，所以这里放一个
 * 空的 base relocation block。
 */
__attribute__((section(".reloc"), used))
static const unsigned int base_reloc_block[] = { 0, 8 };

/*
 * EFI 固件加载 PE/COFF 后进入这里，参数由 UEFI ABI 约定传入。
 *
 * image_handle 代表“当前这个已加载 EFI 镜像”。后面查询 Loaded Image Protocol、
 * 读取启动参数、调用 ExitBootServices 时都会用到它。
 *
 * 如果 efi_main 返回，返回值会交还给启动它的一方：U-Boot bootefi、UEFI Shell
 * 或固件 Boot Manager。真正的 OS loader 通常在 ExitBootServices 后跳进内核，
 * 成功路径不会再返回到这里。
 */
EFI_STATUS efi_main(EFI_HANDLE image_handle, efi_system_table_t *system_table)
{
    struct rvos_boot_info *boot_info = 0;
    UINTN boot_info_pages = 0;
    efi_memory_map_info_t memory_map;

    efi_puts(system_table, L"RVOS EFI boot\r\n");

    EFI_STATUS status = efi_allocate_pages(
        system_table,
        sizeof(*boot_info),
        (void **)&boot_info,
        &boot_info_pages
    );
    if (status != EFI_SUCCESS) {
        efi_puts(system_table, L"Boot info allocation failed, status=");
        efi_print_hex64(system_table, status);
        efi_puts(system_table, L"\r\n");
        return status;
    }

    status = efi_collect_boot_info(image_handle, system_table, boot_info, &memory_map);
    if (status != EFI_SUCCESS) {
        efi_puts(system_table, L"Boot info collection failed, status=");
        efi_print_hex64(system_table, status);
        efi_puts(system_table, L"\r\n");
        efi_free_pages(system_table, boot_info, boot_info_pages);
        return status;
    }

    efi_print_boot_info(system_table, boot_info);
    efi_print_memory_map(system_table, &memory_map);

    efi_free_memory_map(system_table, &memory_map);
    efi_free_pages(system_table, boot_info, boot_info_pages);

    return EFI_SUCCESS;
}

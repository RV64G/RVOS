#include "efi_boot_info.h"

/*
 * RISC-V EFI 镜像必须带一个 .reloc 数据目录。当前程序没有真正需要重定位的
 * 项目，但一些固件/加载器会拒绝完全没有 .reloc 的 PE/COFF，所以这里放一个
 * 空的 base relocation block。
 */
__attribute__((section(".reloc"), used))
static const unsigned int base_reloc_block[] = { 0, 8 };

#define RVOS_KERNEL_STACK_SIZE (32ULL * 1024ULL)

static void print_status(
    efi_system_table_t *system_table,
    const CHAR16 *message,
    EFI_STATUS status
)
{
    efi_puts(system_table, message);
    efi_print_hex64(system_table, status);
    efi_puts(system_table, L"\r\n");
}

static void update_boot_info_memory_map(
    struct rvos_boot_info *boot_info,
    const efi_memory_map_info_t *memory_map
)
{
    boot_info->efi_memory_map_phys = (uint64_t)(uintptr_t)memory_map->buffer;
    boot_info->efi_memory_map_size = memory_map->size;
    boot_info->efi_descriptor_size = memory_map->descriptor_size;
    boot_info->efi_descriptor_version = memory_map->descriptor_version;
    boot_info->flags |= RVOS_BOOT_HAS_EFI_MEMORY_MAP;
}

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
    void *kernel_stack_base = 0;
    UINTN kernel_stack_pages = 0;
    efi_memory_map_info_t memory_map;

    efi_puts(system_table, L"RVOS EFI boot\r\n");

    EFI_STATUS status = efi_allocate_pages(
        system_table,
        sizeof(struct rvos_boot_info),
        (void **)&boot_info,
        &boot_info_pages
    );
    if (status != EFI_SUCCESS) {
        print_status(system_table, L"Boot info allocation failed, status=", status);
        return status;
    }

    status = efi_allocate_pages(
        system_table,
        RVOS_KERNEL_STACK_SIZE,
        &kernel_stack_base,
        &kernel_stack_pages
    );
    if (status != EFI_SUCCESS) {
        print_status(system_table, L"Kernel stack allocation failed, status=", status);
        efi_free_pages(system_table, boot_info, boot_info_pages);
        return status;
    }

    status = efi_collect_boot_info(image_handle, system_table, boot_info, &memory_map);
    if (status != EFI_SUCCESS) {
        print_status(system_table, L"Boot info collection failed, status=", status);
        efi_free_pages(system_table, kernel_stack_base, kernel_stack_pages);
        efi_free_pages(system_table, boot_info, boot_info_pages);
        return status;
    }

    boot_info->kernel_stack_phys = (uint64_t)(uintptr_t)kernel_stack_base;
    boot_info->kernel_stack_size = kernel_stack_pages * EFI_PAGE_SIZE;
    boot_info->flags |= RVOS_BOOT_HAS_KERNEL_STACK;

    efi_print_boot_info(system_table, boot_info);
    efi_print_memory_map(system_table, &memory_map);

    efi_free_memory_map(system_table, &memory_map);

    uint64_t kernel_entry = 0;
    status = efi_load_kernel_elf(image_handle, system_table, &kernel_entry);
    if (status != EFI_SUCCESS) {
        efi_free_pages(system_table, kernel_stack_base, kernel_stack_pages);
        efi_free_pages(system_table, boot_info, boot_info_pages);
        return status;
    }

    /*
     * 打印和释放临时 map 都可能改变 memory map。正式交接前必须重新读取最后一版，
     * 然后不再调用其它 Boot Services，直接 ExitBootServices。
     */
    status = efi_get_memory_map(system_table, &memory_map);
    if (status != EFI_SUCCESS) {
        print_status(system_table, L"Final memory map failed, status=", status);
        efi_free_pages(system_table, kernel_stack_base, kernel_stack_pages);
        efi_free_pages(system_table, boot_info, boot_info_pages);
        return status;
    }

    update_boot_info_memory_map(boot_info, &memory_map);

    status = system_table->boot_services->exit_boot_services(
        image_handle,
        memory_map.map_key
    );
    if (status != EFI_SUCCESS) {
        print_status(system_table, L"ExitBootServices failed, status=", status);
        efi_free_memory_map(system_table, &memory_map);
        efi_free_pages(system_table, kernel_stack_base, kernel_stack_pages);
        efi_free_pages(system_table, boot_info, boot_info_pages);
        return status;
    }

    /*
     * RISC-V 只有通用寄存器 sp，没有单独的“栈基址寄存器”。这里保存低地址基址，
     * 进入内核前把 sp 设到高地址栈顶；栈按 ABI 约定向低地址增长。
     */
    uint8_t *stack_top = (uint8_t *)kernel_stack_base + boot_info->kernel_stack_size;
    rvos_jump_to_kernel((void *)(uintptr_t)kernel_entry, stack_top, boot_info);

    return EFI_SUCCESS;
}

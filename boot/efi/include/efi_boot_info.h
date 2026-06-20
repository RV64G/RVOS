#ifndef EFI_BOOT_INFO_H
#define EFI_BOOT_INFO_H

#include "efi.h"
#include "kernel_boot_info.h"

/**
 * 收集 EFI loader 与内核交接所需的启动信息。
 *
 * @param image_handle 当前 EFI app 的镜像句柄。
 * @param st EFI system table，提供 Boot Services 和 configuration table。
 * @param boot_info 输出参数，写入 RVOS 内核启动 ABI。
 * @param memory_map 输出参数，保存本次读取到的 EFI memory map。
 * @return 成功返回 EFI_SUCCESS；失败返回对应 EFI_STATUS。
 *
 * 这个函数会填充 DTB 地址、boot hart id 和一版 memory map。调用者后续仍需要在
 * ExitBootServices() 前重新读取最终 memory map。
 */
EFI_STATUS efi_collect_boot_info(
    EFI_HANDLE image_handle,
    efi_system_table_t *st,
    struct kernel_boot_info *boot_info,
    efi_memory_map_info_t *memory_map
);

/**
 * 打印 boot_info 摘要。
 *
 * 只用于 EFI 阶段调试输出；退出 Boot Services 后内核不能再调用 EFI console。
 */
void efi_print_boot_info(efi_system_table_t *st, const struct kernel_boot_info *boot_info);

/**
 * 从启动介质装载 RVOS kernel ELF。
 *
 * @param image_handle 当前 EFI app 的镜像句柄，用来定位启动介质。
 * @param st EFI system table。
 * @param entry 输出参数，写入 ELF 入口地址。
 * @param load_start 输出参数，写入所有 PT_LOAD 覆盖到的最小物理地址。
 * @param load_size 输出参数，写入页对齐后的内核装载大小。
 * @return 成功返回 EFI_SUCCESS；失败返回对应 EFI_STATUS。
 *
 * 装载器按 Program Header 中的 PT_LOAD 段工作，而不是按 Section Header 工作。
 */
EFI_STATUS efi_load_kernel_elf(
    EFI_HANDLE image_handle,
    efi_system_table_t *st,
    uint64_t *entry,
    uint64_t *load_start,
    uint64_t *load_size
);

/**
 * 退出 EFI loader 并跳转到 kernel ELF 入口。
 *
 * @param entry kernel ELF 的入口地址。
 * @param stack_top 初始内核栈顶，RISC-V 栈向低地址增长。
 * @param boot_info 传给 kernel_entry() 的启动信息指针。
 *
 * RISC-V 实现会切换 sp、把 boot_info 放入 a0，然后用 jump 交出控制权。固件页表
 * 由 kernel_entry() 在进入内核镜像后关闭，避免 EFI 镜像本身依赖当前页表时出错。
 * 成功路径不返回 EFI loader。
 */
void jump_to_kernel(
    void *entry,
    void *stack_top,
    struct kernel_boot_info *boot_info
);

#endif

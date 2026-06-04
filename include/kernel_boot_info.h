#ifndef KERNEL_BOOT_INFO_H
#define KERNEL_BOOT_INFO_H

#include <stdint.h>

/*
 * "RVOSBOOT" encoded as a little-endian u64.
 *
 * The integer prints as 0x544f4f42534f5652, while the bytes in memory are:
 * 52 56 4f 53 42 4f 4f 54  =>  R V O S B O O T
 */
#define KERNEL_BOOT_INFO_MAGIC 0x544f4f42534f5652ULL
#define KERNEL_BOOT_INFO_VERSION 1U

#define KERNEL_BOOT_HAS_DTB            (1ULL << 0)
#define KERNEL_BOOT_HAS_EFI_MEMORY_MAP (1ULL << 1)
#define KERNEL_BOOT_HAS_BOOT_HART_ID   (1ULL << 2)
#define KERNEL_BOOT_HAS_KERNEL_IMAGE   (1ULL << 3)
#define KERNEL_BOOT_HAS_KERNEL_STACK   (1ULL << 4)

/**
 * EFI loader 传给 RVOS kernel ELF 的启动 ABI。
 *
 * 这个结构由 EFI loader 填写，通过 RISC-V a0 寄存器传给 kernel_entry()。内核只
 * 消费这里记录的普通物理地址和大小，不再调用 EFI Boot Services。
 */
struct kernel_boot_info {
    /** 固定魔数，用来确认指针确实指向 RVOS boot info。 */
    uint64_t magic;

    /** boot info 结构版本。 */
    uint32_t version;

    /** loader 实际写入的结构大小，便于未来兼容扩展。 */
    uint32_t size;

    /** KERNEL_BOOT_HAS_* 组成的有效字段 bitset。 */
    uint64_t flags;

    /** DTB 物理地址；只有 KERNEL_BOOT_HAS_DTB 置位时有效。 */
    uint64_t dtb_phys;

    /** DTB 字节大小；只有 KERNEL_BOOT_HAS_DTB 置位时有效。 */
    uint64_t dtb_size;

    /** 最终 EFI memory map 的物理地址。 */
    uint64_t efi_memory_map_phys;

    /** 最终 EFI memory map 的字节大小。 */
    uint64_t efi_memory_map_size;

    /** EFI memory descriptor 步长，遍历 memory map 时必须按这个值前进。 */
    uint64_t efi_descriptor_size;

    /** EFI memory descriptor 版本。 */
    uint32_t efi_descriptor_version;
    uint32_t reserved0;

    /** 固件选择进入下一阶段的 boot hart id。 */
    uint64_t boot_hart_id;

    /** kernel ELF PT_LOAD 覆盖到的物理起始地址。 */
    uint64_t kernel_phys_base;

    /** kernel ELF 页对齐后的装载大小。 */
    uint64_t kernel_size;

    /** boot_info 自己所在物理页。 */
    uint64_t boot_info_phys;

    /** boot_info 预留大小，当前按页记录。 */
    uint64_t boot_info_size;

    /** 初始内核栈物理起始地址。 */
    uint64_t kernel_stack_phys;

    /** 初始内核栈大小，栈顶为 kernel_stack_phys + kernel_stack_size。 */
    uint64_t kernel_stack_size;
};

#endif

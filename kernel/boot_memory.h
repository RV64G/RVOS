#ifndef KERNEL_BOOT_MEMORY_H
#define KERNEL_BOOT_MEMORY_H

#include <stdint.h>

#include "kernel_boot_info.h"

#define BOOT_MAX_USABLE_RANGES 16
#define BOOT_MAX_RESERVED_RANGES 8

struct phys_range {
    /** 起始物理地址，包含，必须 4KB 对齐。 */
    uint64_t start;

    /** 结束物理地址，不包含，必须 4KB 对齐。 */
    uint64_t end;
};

/**
 * 内核从最终 EFI memory map 整理出的启动内存快照。
 *
 * 这不是最终物理页分配器，只记录启动期哪些范围可以导入 page_alloc，哪些范围必须
 * 保留。page_allocator_init() 会消费 usable_ranges。
 */
struct boot_memory_state {
    /** EFI memory descriptor 数量。 */
    uint64_t entries;

    /** EFI memory descriptor 步长。 */
    uint64_t descriptor_size;

    /** EFI ConventionalMemory 总页数。 */
    uint64_t conventional_pages;

    /** EFI LoaderCode/LoaderData 总页数。 */
    uint64_t loader_pages;

    /** EFI BootServicesCode/BootServicesData 总页数。 */
    uint64_t boot_services_pages;

    /** EFI RuntimeServicesCode/RuntimeServicesData 总页数。 */
    uint64_t runtime_pages;

    /** 当前允许交给物理页分配器的 range 数量。 */
    uint64_t usable_range_count;
    struct phys_range usable_ranges[BOOT_MAX_USABLE_RANGES];

    /** 启动期必须保留的 range 数量。 */
    uint64_t reserved_range_count;
    struct phys_range reserved_ranges[BOOT_MAX_RESERVED_RANGES];
};

/**
 * 返回当前启动内存快照。
 */
const struct boot_memory_state *memory_state(void);

/**
 * 解析 boot_info 中的最终 EFI memory map，整理 usable/reserved ranges。
 *
 * @param boot_info EFI loader 传入的启动 ABI。
 * @return 成功返回 1，失败返回 0。
 */
int memory_probe(const struct kernel_boot_info *boot_info);

/**
 * 返回当前启动内存快照中可交给页分配器的 4KB 页数。
 */
uint64_t memory_available_pages(void);

#endif

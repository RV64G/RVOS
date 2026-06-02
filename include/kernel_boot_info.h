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

struct kernel_boot_info {
    uint64_t magic;
    uint32_t version;
    uint32_t size;
    uint64_t flags;

    uint64_t dtb_phys;
    uint64_t dtb_size;

    uint64_t efi_memory_map_phys;
    uint64_t efi_memory_map_size;
    uint64_t efi_descriptor_size;
    uint32_t efi_descriptor_version;
    uint32_t reserved0;

    uint64_t boot_hart_id;

    uint64_t kernel_phys_base;
    uint64_t kernel_size;

    uint64_t boot_info_phys;
    uint64_t boot_info_size;

    uint64_t kernel_stack_phys;
    uint64_t kernel_stack_size;
};

#endif

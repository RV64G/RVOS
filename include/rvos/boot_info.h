#ifndef RVOS_BOOT_INFO_H
#define RVOS_BOOT_INFO_H

typedef unsigned long long rvos_u64;
typedef unsigned int rvos_u32;

#define RVOS_BOOT_INFO_MAGIC 0x544f4f42534f5652ULL
#define RVOS_BOOT_INFO_VERSION 1U

#define RVOS_BOOT_HAS_DTB            (1ULL << 0)
#define RVOS_BOOT_HAS_EFI_MEMORY_MAP (1ULL << 1)
#define RVOS_BOOT_HAS_BOOT_HART_ID   (1ULL << 2)
#define RVOS_BOOT_HAS_KERNEL_IMAGE   (1ULL << 3)
#define RVOS_BOOT_HAS_KERNEL_STACK   (1ULL << 4)

struct rvos_boot_info {
    rvos_u64 magic;
    rvos_u32 version;
    rvos_u32 size;
    rvos_u64 flags;

    rvos_u64 dtb_phys;
    rvos_u64 dtb_size;

    rvos_u64 efi_memory_map_phys;
    rvos_u64 efi_memory_map_size;
    rvos_u64 efi_descriptor_size;
    rvos_u32 efi_descriptor_version;
    rvos_u32 reserved0;

    rvos_u64 boot_hart_id;

    rvos_u64 kernel_phys_base;
    rvos_u64 kernel_size;

    rvos_u64 boot_info_phys;
    rvos_u64 boot_info_size;

    rvos_u64 kernel_stack_phys;
    rvos_u64 kernel_stack_size;
};

#endif

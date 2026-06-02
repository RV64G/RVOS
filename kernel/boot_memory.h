#ifndef KERNEL_BOOT_MEMORY_H
#define KERNEL_BOOT_MEMORY_H

#include <stdint.h>

#include "kernel_boot_info.h"

#define BOOT_MAX_USABLE_RANGES 16
#define BOOT_MAX_RESERVED_RANGES 8

struct phys_range {
    uint64_t start;
    uint64_t end;
};

struct boot_memory_state {
    uint64_t entries;
    uint64_t descriptor_size;

    uint64_t conventional_pages;
    uint64_t loader_pages;
    uint64_t boot_services_pages;
    uint64_t runtime_pages;

    uint64_t usable_range_count;
    struct phys_range usable_ranges[BOOT_MAX_USABLE_RANGES];

    uint64_t reserved_range_count;
    struct phys_range reserved_ranges[BOOT_MAX_RESERVED_RANGES];
};

const struct boot_memory_state *memory_state(void);
int memory_probe(const struct kernel_boot_info *boot_info);
uint64_t memory_available_pages(void);

#endif

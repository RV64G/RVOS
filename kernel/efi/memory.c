#include "early_log.h"
#include "memory.h"

#define EFI_LOADER_CODE                1
#define EFI_LOADER_DATA                2
#define EFI_BOOT_SERVICES_CODE         3
#define EFI_BOOT_SERVICES_DATA         4
#define EFI_RUNTIME_SERVICES_CODE      5
#define EFI_RUNTIME_SERVICES_DATA      6
#define EFI_CONVENTIONAL_MEMORY        7

#define PAGE_SIZE 4096ULL
#define MAX_EARLY_USABLE_RANGES 16

/*
 * UEFI memory descriptor 的前几个字段是规范固定布局。descriptor_size 可能比这个
 * 结构体大，所以遍历 memory map 时必须按 boot_info 里的 descriptor_size 前进。
 */
struct efi_memory_descriptor {
    uint32_t type;
    uint32_t padding;
    uint64_t physical_start;
    uint64_t virtual_start;
    uint64_t number_of_pages;
    uint64_t attribute;
};

struct phys_range {
    uint64_t start;
    uint64_t end;
};

static uint64_t align_up(uint64_t value, uint64_t align)
{
    return (value + align - 1) & ~(align - 1);
}

static uint64_t range_end(uint64_t start, uint64_t pages)
{
    return start + pages * PAGE_SIZE;
}

static int range_is_valid(uint64_t start, uint64_t end)
{
    return start != 0 && end > start;
}

static void print_range(const char *name, uint64_t start, uint64_t end)
{
    early_puts("  ");
    early_puts(name);
    early_puts(": ");
    early_print_hex64(start);
    early_puts("..");
    early_print_hex64(end);
    early_puts(", pages=");
    early_print_hex64((end - start) / PAGE_SIZE);
    early_puts("\r\n");
}

static void print_reserved_boot_ranges(const struct rvos_boot_info *boot_info)
{
    early_puts("Reserved boot ranges\r\n");

    uint64_t kernel_end = boot_info->kernel_phys_base + boot_info->kernel_size;
    /*
     * boot_info 里保存的是 memory map 的实际字节数，不是 EFI 分配的页数。
     * EFI allocation 按页发生，内核保留时也必须向上扩到整页。
     */
    uint64_t map_end = align_up(
        boot_info->efi_memory_map_phys + boot_info->efi_memory_map_size,
        PAGE_SIZE
    );
    uint64_t info_end = boot_info->boot_info_phys + boot_info->boot_info_size;
    uint64_t stack_end = boot_info->kernel_stack_phys + boot_info->kernel_stack_size;

    if (range_is_valid(boot_info->kernel_phys_base, kernel_end)) {
        print_range("kernel", boot_info->kernel_phys_base, kernel_end);
    }
    if (range_is_valid(boot_info->efi_memory_map_phys, map_end)) {
        print_range("efi_memory_map", boot_info->efi_memory_map_phys, map_end);
    }
    if (range_is_valid(boot_info->boot_info_phys, info_end)) {
        print_range("boot_info", boot_info->boot_info_phys, info_end);
    }
    if (range_is_valid(boot_info->kernel_stack_phys, stack_end)) {
        print_range("kernel_stack", boot_info->kernel_stack_phys, stack_end);
    }
}

static void add_usable_range(
    struct phys_range *ranges,
    uint64_t *range_count,
    uint64_t start,
    uint64_t end
)
{
    if (!range_is_valid(start, end)) {
        return;
    }

    if (*range_count > 0 && ranges[*range_count - 1].end == start) {
        ranges[*range_count - 1].end = end;
        return;
    }

    if (*range_count >= MAX_EARLY_USABLE_RANGES) {
        return;
    }

    ranges[*range_count] = (struct phys_range){
        .start = start,
        .end = end,
    };
    *range_count += 1;
}

static const struct efi_memory_descriptor *descriptor_at(
    const struct rvos_boot_info *boot_info,
    uint64_t index
)
{
    const uint8_t *base = (const uint8_t *)(uintptr_t)boot_info->efi_memory_map_phys;
    return (const struct efi_memory_descriptor *)(
        base + index * boot_info->efi_descriptor_size
    );
}

void rvos_early_memory_probe(const struct rvos_boot_info *boot_info)
{
    if (boot_info->efi_descriptor_size < sizeof(struct efi_memory_descriptor)) {
        early_puts("EFI memory map rejected: descriptor too small\r\n");
        early_print_field("descriptor_size", boot_info->efi_descriptor_size);
        return;
    }

    uint64_t entries = boot_info->efi_memory_map_size / boot_info->efi_descriptor_size;
    uint64_t conventional_pages = 0;
    uint64_t loader_pages = 0;
    uint64_t boot_services_pages = 0;
    uint64_t runtime_pages = 0;
    struct phys_range usable_ranges[MAX_EARLY_USABLE_RANGES];
    uint64_t usable_range_count = 0;

    /*
     * 当前只把 Conventional Memory 作为“早期可用内存”打印出来。LoaderData 和
     * BootServicesData 在 ExitBootServices 后并非永远不能用，但里面混有 kernel、
     * boot_info、内核栈和最终 memory map 这些启动保留页。等页分配器接入时，再做
     * 更完整的 reclaim/subtract 逻辑。
     */
    for (uint64_t i = 0; i < entries; i++) {
        const struct efi_memory_descriptor *desc = descriptor_at(boot_info, i);
        uint64_t start = desc->physical_start;
        uint64_t end = range_end(start, desc->number_of_pages);

        switch (desc->type) {
        case EFI_CONVENTIONAL_MEMORY:
            conventional_pages += desc->number_of_pages;
            add_usable_range(&usable_ranges[0], &usable_range_count, start, end);
            break;
        case EFI_LOADER_CODE:
        case EFI_LOADER_DATA:
            loader_pages += desc->number_of_pages;
            break;
        case EFI_BOOT_SERVICES_CODE:
        case EFI_BOOT_SERVICES_DATA:
            boot_services_pages += desc->number_of_pages;
            break;
        case EFI_RUNTIME_SERVICES_CODE:
        case EFI_RUNTIME_SERVICES_DATA:
            runtime_pages += desc->number_of_pages;
            break;
        default:
            break;
        }
    }

    early_puts("EFI memory map accepted\r\n");
    early_print_field("entries", entries);
    early_print_field("descriptor_size", boot_info->efi_descriptor_size);
    early_print_field("conventional_pages", conventional_pages);
    early_print_field("loader_pages", loader_pages);
    early_print_field("boot_services_pages", boot_services_pages);
    early_print_field("runtime_pages", runtime_pages);

    print_reserved_boot_ranges(boot_info);

    early_puts("Early usable conventional ranges\r\n");
    early_print_field("range_count", usable_range_count);
    for (uint64_t i = 0; i < usable_range_count; i++) {
        print_range("usable", usable_ranges[i].start, usable_ranges[i].end);
    }
    if (usable_range_count == MAX_EARLY_USABLE_RANGES) {
        early_puts("  usable range output truncated\r\n");
    }

    early_puts("  other memory types are kept reserved for now\r\n");
}

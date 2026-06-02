#include "early_log.h"
#include "boot_memory.h"

#define EFI_LOADER_CODE                1
#define EFI_LOADER_DATA                2
#define EFI_BOOT_SERVICES_CODE         3
#define EFI_BOOT_SERVICES_DATA         4
#define EFI_RUNTIME_SERVICES_CODE      5
#define EFI_RUNTIME_SERVICES_DATA      6
#define EFI_CONVENTIONAL_MEMORY        7

#define PAGE_SIZE 4096ULL

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

static struct boot_memory_state state;

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

static void add_range(
    struct phys_range *ranges,
    uint64_t max_ranges,
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

    if (*range_count >= max_ranges) {
        return;
    }

    ranges[*range_count] = (struct phys_range){
        .start = start,
        .end = end,
    };
    *range_count += 1;
}

static void add_reserved_range(uint64_t start, uint64_t end)
{
    add_range(
        &state.reserved_ranges[0],
        BOOT_MAX_RESERVED_RANGES,
        &state.reserved_range_count,
        start,
        end
    );
}

static void add_usable_range(uint64_t start, uint64_t end)
{
    add_range(
        &state.usable_ranges[0],
        BOOT_MAX_USABLE_RANGES,
        &state.usable_range_count,
        start,
        end
    );
}

static void collect_reserved_boot_ranges(const struct kernel_boot_info *boot_info)
{
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

    add_reserved_range(boot_info->kernel_phys_base, kernel_end);
    add_reserved_range(boot_info->efi_memory_map_phys, map_end);
    add_reserved_range(boot_info->boot_info_phys, info_end);
    add_reserved_range(boot_info->kernel_stack_phys, stack_end);
}

static const struct efi_memory_descriptor *descriptor_at(
    const struct kernel_boot_info *boot_info,
    uint64_t index
)
{
    const uint8_t *base = (const uint8_t *)(uintptr_t)boot_info->efi_memory_map_phys;
    return (const struct efi_memory_descriptor *)(
        base + index * boot_info->efi_descriptor_size
    );
}

static void print_memory_state(void)
{
    uint64_t available_pages = memory_available_pages();

    early_puts("EFI memory map accepted\r\n");
    early_print_field("entries", state.entries);
    early_print_field("descriptor_size", state.descriptor_size);
    early_print_field("conventional_pages", state.conventional_pages);
    early_print_dec_field("conventional_mib", state.conventional_pages / 256);
    early_print_field("loader_pages", state.loader_pages);
    early_print_field("boot_services_pages", state.boot_services_pages);
    early_print_field("runtime_pages", state.runtime_pages);

    early_puts("Reserved boot ranges\r\n");
    early_print_field("range_count", state.reserved_range_count);
    for (uint64_t i = 0; i < state.reserved_range_count; i++) {
        print_range(
            "reserved",
            state.reserved_ranges[i].start,
            state.reserved_ranges[i].end
        );
    }
    if (state.reserved_range_count == BOOT_MAX_RESERVED_RANGES) {
        early_puts("  reserved range output truncated\r\n");
    }

    early_puts("Boot usable conventional ranges\r\n");
    early_print_field("range_count", state.usable_range_count);
    early_print_field("available_pages", available_pages);
    early_print_dec_field("available_mib", available_pages / 256);
    for (uint64_t i = 0; i < state.usable_range_count; i++) {
        print_range("usable", state.usable_ranges[i].start, state.usable_ranges[i].end);
    }
    if (state.usable_range_count == BOOT_MAX_USABLE_RANGES) {
        early_puts("  usable range output truncated\r\n");
    }

    early_puts("  other memory types are kept reserved for now\r\n");
}

static void reset_memory_state(void)
{
    state.entries = 0;
    state.descriptor_size = 0;
    state.conventional_pages = 0;
    state.loader_pages = 0;
    state.boot_services_pages = 0;
    state.runtime_pages = 0;
    state.usable_range_count = 0;
    state.reserved_range_count = 0;

    for (uint64_t i = 0; i < BOOT_MAX_USABLE_RANGES; i++) {
        state.usable_ranges[i].start = 0;
        state.usable_ranges[i].end = 0;
    }

    for (uint64_t i = 0; i < BOOT_MAX_RESERVED_RANGES; i++) {
        state.reserved_ranges[i].start = 0;
        state.reserved_ranges[i].end = 0;
    }
}

const struct boot_memory_state *memory_state(void)
{
    return &state;
}

uint64_t memory_available_pages(void)
{
    uint64_t pages = 0;
    for (uint64_t i = 0; i < state.usable_range_count; i++) {
        pages += (state.usable_ranges[i].end - state.usable_ranges[i].start) / PAGE_SIZE;
    }
    return pages;
}

int memory_probe(const struct kernel_boot_info *boot_info)
{
    reset_memory_state();

    if (boot_info->efi_descriptor_size < sizeof(struct efi_memory_descriptor)) {
        early_puts("EFI memory map rejected: descriptor too small\r\n");
        early_print_field("descriptor_size", boot_info->efi_descriptor_size);
        return 0;
    }

    state.entries = boot_info->efi_memory_map_size / boot_info->efi_descriptor_size;
    state.descriptor_size = boot_info->efi_descriptor_size;
    collect_reserved_boot_ranges(boot_info);

    /*
     * 当前只把 Conventional Memory 作为“启动阶段可用内存”打印出来。LoaderData 和
     * BootServicesData 在 ExitBootServices 后并非永远不能用，但里面混有 kernel、
     * boot_info、内核栈和最终 memory map 这些启动保留页。等页分配器接入时，再做
     * 更完整的 reclaim/subtract 逻辑。
     */
    for (uint64_t i = 0; i < state.entries; i++) {
        const struct efi_memory_descriptor *desc = descriptor_at(boot_info, i);
        uint64_t start = desc->physical_start;
        uint64_t end = range_end(start, desc->number_of_pages);

        switch (desc->type) {
        case EFI_CONVENTIONAL_MEMORY:
            state.conventional_pages += desc->number_of_pages;
            add_usable_range(start, end);
            break;
        case EFI_LOADER_CODE:
        case EFI_LOADER_DATA:
            state.loader_pages += desc->number_of_pages;
            break;
        case EFI_BOOT_SERVICES_CODE:
        case EFI_BOOT_SERVICES_DATA:
            state.boot_services_pages += desc->number_of_pages;
            break;
        case EFI_RUNTIME_SERVICES_CODE:
        case EFI_RUNTIME_SERVICES_DATA:
            state.runtime_pages += desc->number_of_pages;
            break;
        default:
            break;
        }
    }

    print_memory_state();
    return 1;
}

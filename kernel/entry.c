#include "early_log.h"
#include "boot_memory.h"
#include "page_alloc.h"
#include "early_vm.h"
#include "kernel_boot_info.h"

/*
 * boot_info->flags 是一个 bitset。loader 只有在某类启动信息有效时才会置位，
 * 内核打印和校验时用这个 helper 判断“这项信息是否真的可用”。
 */
static int boot_info_has(const struct kernel_boot_info *boot_info, uint64_t flag)
{
    return (boot_info->flags & flag) != 0;
}

static void print_boot_flag(
    const struct kernel_boot_info *boot_info,
    const char *name,
    uint64_t flag
)
{
    early_puts("    ");
    early_puts(name);
    early_puts(boot_info_has(boot_info, flag) ? ": yes\r\n" : ": no\r\n");
}

static void print_boot_info(const struct kernel_boot_info *boot_info)
{
    early_puts("Boot info accepted\r\n");
    early_print_field("magic", boot_info->magic);
    early_print_field("version", boot_info->version);
    early_print_field("size", boot_info->size);
    early_print_field("flags", boot_info->flags);

    early_puts("  flags detail:\r\n");
    print_boot_flag(boot_info, "dtb", KERNEL_BOOT_HAS_DTB);
    print_boot_flag(boot_info, "efi_memory_map", KERNEL_BOOT_HAS_EFI_MEMORY_MAP);
    print_boot_flag(boot_info, "boot_hart_id", KERNEL_BOOT_HAS_BOOT_HART_ID);
    print_boot_flag(boot_info, "kernel_image", KERNEL_BOOT_HAS_KERNEL_IMAGE);
    print_boot_flag(boot_info, "kernel_stack", KERNEL_BOOT_HAS_KERNEL_STACK);

    early_print_field("boot_hart_id", boot_info->boot_hart_id);
    early_print_field("dtb_phys", boot_info->dtb_phys);
    early_print_field("dtb_size", boot_info->dtb_size);
    early_print_field("efi_memory_map_phys", boot_info->efi_memory_map_phys);
    early_print_field("efi_memory_map_size", boot_info->efi_memory_map_size);
    early_print_field("efi_descriptor_size", boot_info->efi_descriptor_size);
    early_print_field("efi_descriptor_version", boot_info->efi_descriptor_version);
    early_print_field("kernel_phys_base", boot_info->kernel_phys_base);
    early_print_field("kernel_size", boot_info->kernel_size);
    early_print_field("boot_info_phys", boot_info->boot_info_phys);
    early_print_field("boot_info_size", boot_info->boot_info_size);
    early_print_field("kernel_stack_phys", boot_info->kernel_stack_phys);
    early_print_field("kernel_stack_size", boot_info->kernel_stack_size);
}

static int validate_boot_info(const struct kernel_boot_info *boot_info)
{
    /*
     * boot_info 是 EFI loader 和内核之间的启动 ABI。这里先只校验进入内核必须依赖
     * 的字段：最终 EFI memory map、boot hart id、内核镜像范围和早期内核栈。
     * DTB 暂时不作为硬性要求，因为后面还要单独决定设备发现策略。
     */
    if (!boot_info) {
        early_puts("Boot info rejected: null pointer\r\n");
        return 0;
    }

    if (boot_info->magic != KERNEL_BOOT_INFO_MAGIC) {
        early_puts("Boot info rejected: bad magic\r\n");
        early_print_field("magic", boot_info->magic);
        return 0;
    }

    if (boot_info->version != KERNEL_BOOT_INFO_VERSION) {
        early_puts("Boot info rejected: unsupported version\r\n");
        early_print_field("version", boot_info->version);
        return 0;
    }

    if (boot_info->size < sizeof(*boot_info)) {
        early_puts("Boot info rejected: structure too small\r\n");
        early_print_field("size", boot_info->size);
        return 0;
    }

    uint64_t required_flags =
        KERNEL_BOOT_HAS_EFI_MEMORY_MAP |
        KERNEL_BOOT_HAS_BOOT_HART_ID |
        KERNEL_BOOT_HAS_KERNEL_IMAGE |
        KERNEL_BOOT_HAS_KERNEL_STACK;
    if ((boot_info->flags & required_flags) != required_flags) {
        early_puts("Boot info rejected: required flags missing\r\n");
        early_print_field("flags", boot_info->flags);
        return 0;
    }

    if (boot_info->efi_memory_map_phys == 0 ||
        boot_info->efi_memory_map_size == 0 ||
        boot_info->efi_descriptor_size == 0 ||
        boot_info->kernel_phys_base == 0 ||
        boot_info->kernel_size == 0 ||
        boot_info->kernel_stack_phys == 0 ||
        boot_info->kernel_stack_size == 0) {
        early_puts("Boot info rejected: required range is empty\r\n");
        return 0;
    }

    return 1;
}

/*
 * EFI loader 跳进来的第一个内核入口。
 *
 * 当前阶段还没有串口驱动、页分配器和 printk，所以这里直接通过 SBI debug console
 * 打印最早期日志。等内核接管设备和内存后，这些临时打印函数应该退到正式 printk
 * 后面，入口也会继续拆出 arch 级初始化和通用内核初始化。
 */
void kernel_entry(struct kernel_boot_info *boot_info)
{
    sbi_console_puts("\r\nKernel ELF entered\r\n");

    if (!validate_boot_info(boot_info)) {
        early_halt_forever();
    }

    print_boot_info(boot_info);
    if (!memory_probe(boot_info)) {
        early_halt_forever();
    }
    if (!page_allocator_init()) {
        early_halt_forever();
    }
    if (!early_vm_enable(boot_info)) {
        early_halt_forever();
    }
    early_halt_forever();
}

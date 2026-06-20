#include <stdint.h>

#include "early_log.h"
#include "boot_memory.h"
#include "vm.h"
#include "early_vm.h"

extern char __kernel_text_start[];
extern char __kernel_text_end[];
extern char __kernel_rodata_start[];
extern char __kernel_rodata_end[];
extern char __initramfs_start[];
extern char __initramfs_end[];
extern char __kernel_data_start[];
extern char __kernel_data_end[];
extern char __kernel_bss_start[];
extern char __kernel_bss_end[];

/*
 * early_vm 只负责“第一张能让内核继续跑的页表”。
 *
 * 真正的页表操作已经下沉到 vm.c。这里保留启动期职责：选择必须立即可访问的内核
 * section、启动信息、DTB、EFI memory map 和当前可用物理内存，然后激活 Sv39。
 */
static struct vm_space kernel_vm;

static int map_kernel_sections(void)
{
    /*
     * 链接脚本把这些 section 边界按 4KB 对齐。同一物理页只能有一条 PTE，因此如果
     * text 和 rodata/data 混在同一页，就无法可靠地拆权限。
     */
    if (!vm_identity_map(
            &kernel_vm,
            (uint64_t)(uintptr_t)__kernel_text_start,
            (uint64_t)(__kernel_text_end - __kernel_text_start),
            VM_MAP_READ | VM_MAP_EXEC)) {
        return 0;
    }

    if (!vm_identity_map(
            &kernel_vm,
            (uint64_t)(uintptr_t)__kernel_rodata_start,
            (uint64_t)(__kernel_rodata_end - __kernel_rodata_start),
            VM_MAP_READ)) {
        return 0;
    }

    /*
     * .initramfs 是内核 ELF 携带的只读启动文件包。S-mode 内核只需要查询其中的
     * 用户程序，再交给 ELF loader 复制到用户地址空间。
     */
    if (!vm_identity_map(
            &kernel_vm,
            (uint64_t)(uintptr_t)__initramfs_start,
            (uint64_t)(__initramfs_end - __initramfs_start),
            VM_MAP_READ)) {
        return 0;
    }

    if (!vm_identity_map(
            &kernel_vm,
            (uint64_t)(uintptr_t)__kernel_data_start,
            (uint64_t)(__kernel_data_end - __kernel_data_start),
            VM_MAP_READ | VM_MAP_WRITE)) {
        return 0;
    }

    if (!vm_identity_map(
            &kernel_vm,
            (uint64_t)(uintptr_t)__kernel_bss_start,
            (uint64_t)(__kernel_bss_end - __kernel_bss_start),
            VM_MAP_READ | VM_MAP_WRITE)) {
        return 0;
    }

    return 1;
}

static int map_boot_info_ranges(const struct kernel_boot_info *boot_info)
{
    /*
     * 这些范围是打开页表前后都会立刻访问的启动材料，必须先映射：
     *
     * kernel stack      : 当前 sp 正在使用的栈
     * boot_info         : 入口参数本身
     * EFI memory map    : memory_state/page allocator 的来源
     * DTB               : 后续设备发现会用到
     */
    if (!vm_identity_map(
            &kernel_vm,
            boot_info->kernel_stack_phys,
            boot_info->kernel_stack_size,
            VM_MAP_READ | VM_MAP_WRITE)) {
        return 0;
    }

    if (!vm_identity_map(
            &kernel_vm,
            boot_info->boot_info_phys,
            boot_info->boot_info_size,
            VM_MAP_READ | VM_MAP_WRITE)) {
        return 0;
    }

    if (!vm_identity_map(
            &kernel_vm,
            boot_info->efi_memory_map_phys,
            boot_info->efi_memory_map_size,
            VM_MAP_READ)) {
        return 0;
    }

    if ((boot_info->flags & KERNEL_BOOT_HAS_DTB) != 0) {
        if (!vm_identity_map(
                &kernel_vm,
                boot_info->dtb_phys,
                boot_info->dtb_size,
                VM_MAP_READ)) {
            return 0;
        }
    }

    return 1;
}

static int map_known_physical_memory(void)
{
    /*
     * 后续打开 Sv39 后，物理页分配器仍会返回“物理页地址”。在正式引入
     * phys_to_virt/direct map 边界之前，先把当前认为可用的物理区间恒等映射，
     * 确保新分配的页仍能被内核代码访问。
     */
    const struct boot_memory_state *memory = memory_state();
    for (uint64_t i = 0; i < memory->usable_range_count; i++) {
        uint64_t start = memory->usable_ranges[i].start;
        uint64_t size = memory->usable_ranges[i].end - memory->usable_ranges[i].start;
        if (!vm_identity_map(&kernel_vm, start, size, VM_MAP_READ | VM_MAP_WRITE)) {
            return 0;
        }
    }

    return 1;
}

struct vm_space *kernel_vm_space(void)
{
    return &kernel_vm;
}

int early_vm_enable(const struct kernel_boot_info *boot_info)
{
    if (!vm_space_create(&kernel_vm)) {
        early_puts("Kernel page table allocation failed\r\n");
        return 0;
    }

    if (!map_kernel_sections()) {
        early_puts("Kernel section mapping failed\r\n");
        return 0;
    }
    if (!map_boot_info_ranges(boot_info)) {
        early_puts("Kernel boot ranges mapping failed\r\n");
        return 0;
    }
    if (!map_known_physical_memory()) {
        early_puts("Kernel memory ranges mapping failed\r\n");
        return 0;
    }

    uint64_t root_phys = (uint64_t)(uintptr_t)kernel_vm.root_table;

    early_puts("Kernel page table prepared\r\n");
    early_print_field("root_table", root_phys);
    early_print_field("page_table_pages", kernel_vm.page_table_pages);
    early_print_field("mapped_ranges", kernel_vm.mapped_ranges);
    early_print_field("mapped_pages", kernel_vm.mapped_pages);

    vm_activate_sv39(&kernel_vm);

    early_puts("Kernel Sv39 enabled\r\n");
    return 1;
}

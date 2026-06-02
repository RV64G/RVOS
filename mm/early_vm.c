#include <stdint.h>

#include "early_log.h"
#include "boot_memory.h"
#include "page_alloc.h"
#include "early_vm.h"

#define PAGE_SIZE 4096ULL
#define PTE_COUNT 512ULL

#define SV39_MODE 8ULL
#define SATP_MODE_SHIFT 60

/*
 * RISC-V Sv39 PTE 低 10 位保存权限和状态位，高位保存物理页号 PPN。
 *
 * V(valid) 表示这条 PTE 有效。R/W/X 同时全为 0 时，PTE 指向下一级页表；
 * 只要 R/W/X 至少有一位为 1，它就是叶子映射。A(accessed) 和 D(dirty)
 * 由硬件或异常处理路径维护。当前第一版页表先直接置 A/D，避免刚打开页表就因为
 * 访问/写入状态位缺失而进入缺页处理；正式缺页异常接入后可以再改成懒维护。
 */
#define PTE_V (1ULL << 0)
#define PTE_R (1ULL << 1)
#define PTE_W (1ULL << 2)
#define PTE_X (1ULL << 3)
#define PTE_A (1ULL << 6)
#define PTE_D (1ULL << 7)

#define PAGE_4K_SHIFT 12
#define PAGE_2M_SHIFT 21
#define PAGE_1G_SHIFT 30

#define PAGE_4K_SIZE (1ULL << PAGE_4K_SHIFT)
#define PAGE_2M_SIZE (1ULL << PAGE_2M_SHIFT)
#define PAGE_1G_SIZE (1ULL << PAGE_1G_SHIFT)

typedef uint64_t pte_t;

/*
 * early_vm 只负责“第一张能让内核继续跑的页表”。
 *
 * 它不是最终 mm/vm 层：这里没有用户页表、权限拆分、direct map 抽象，也没有缺页
 * 异常配合。当前目标是打开 Sv39 后，内核代码、当前栈、boot_info、最终 memory map
 * 和后续新分配的物理页仍然可访问。
 */
static pte_t *kernel_root_table;
static uint64_t mapped_ranges;
static uint64_t mapped_pages;
static uint64_t page_table_pages;

static uint64_t align_down(uint64_t value, uint64_t align)
{
    return value & ~(align - 1);
}

static uint64_t align_up(uint64_t value, uint64_t align)
{
    return (value + align - 1) & ~(align - 1);
}

static int is_aligned(uint64_t value, uint64_t align)
{
    return (value & (align - 1)) == 0;
}

static uint64_t pte_to_phys(pte_t pte)
{
    /*
     * PTE[53:10] 保存物理页号 PPN。转回物理地址时把 PPN 移回 4KB 页偏移上方。
     */
    return (pte >> 10) << PAGE_4K_SHIFT;
}

static pte_t phys_to_pte(uint64_t phys, uint64_t flags)
{
    /*
     * 物理地址先去掉 12 位页内偏移，变成 PPN，再放到 PTE[53:10]。
     * flags 只占低位权限/状态位。
     */
    return ((phys >> PAGE_4K_SHIFT) << 10) | flags;
}

static uint64_t vpn_index(uint64_t va, int level)
{
    /*
     * Sv39 把虚拟页号拆成三级索引：
     *
     * level 2: VA[38:30]，根页表索引
     * level 1: VA[29:21]，中间页表索引
     * level 0: VA[20:12]，最低级页表索引
     *
     * 每张页表 4KB，512 项，每项 8 字节，索引宽度固定是 9 bit。
     */
    return (va >> (PAGE_4K_SHIFT + 9 * level)) & (PTE_COUNT - 1);
}

static void zero_page(void *page)
{
    uint64_t *words = (uint64_t *)page;
    for (uint64_t i = 0; i < PAGE_SIZE / sizeof(uint64_t); i++)
    {
        words[i] = 0;
    }
}

static pte_t *allocate_page_table(void)
{
    void *page = phys_alloc_pages(1);
    if (!page)
    {
        return 0;
    }

    /*
     * 打开 Sv39 之前，当前执行环境仍是物理地址直通。phys_alloc_pages() 返回的
     * 物理页可以直接当指针清零。打开页表之后，CPU page walker 也按物理地址读取
     * 页表页，所以页表页本身不需要再“转换成虚拟指针”才能被硬件使用。
     */
    zero_page(page);
    page_table_pages++;
    return (pte_t *)page;
}

static int ensure_next_table(pte_t *table, uint64_t index, pte_t **next)
{
    /*
     * map_leaf() 从根页表一路走到目标 level。中间层 PTE 必须指向“下一张页表”。
     * ensure_next_table() 做的就是：
     *
     * 1. 如果 table[index] 已经是一个非叶子 PTE，就取出它指向的下一级页表；
     * 2. 如果 table[index] 为空，就分配一页新的页表页，写成非叶子 PTE；
     * 3. 如果 table[index] 已经是叶子映射，说明这里不能再往下挂页表，拒绝。
     *
     * 例子：要映射一个 4KB 页，就需要 level2 -> level1 -> level0。前两级如果不
     * 存在，就由这个函数补出来。
     */
    pte_t pte = table[index];
    if (pte & PTE_V)
    {
        /*
         * 当前阶段只由本文件创建页表。如果这里已经是叶子映射，说明调用者试图在
         * 已映射的大页下面继续拆小页，第一版直接拒绝，避免静默覆盖映射。
         */
        if (pte & (PTE_R | PTE_W | PTE_X))
        {
            return 0;
        }

        *next = (pte_t *)(uintptr_t)pte_to_phys(pte);
        return 1;
    }

    pte_t *new_table = allocate_page_table();
    if (!new_table)
    {
        return 0;
    }

    table[index] = phys_to_pte((uint64_t)(uintptr_t)new_table, PTE_V);
    *next = new_table;
    return 1;
}

static int map_leaf(uint64_t va, uint64_t pa, uint64_t page_size, int level)
{
    /*
     * 在指定 level 写入叶子 PTE：
     *
     * level 2 leaf: 1GB 映射
     * level 1 leaf: 2MB 映射
     * level 0 leaf: 4KB 映射
     *
     * 中间层如果不存在，就通过 ensure_next_table() 创建；最终 level 的 PTE 写入
     * pa + 权限位，成为真正的地址映射。
     */
    pte_t *table = kernel_root_table;

    for (int current = 2; current > level; current--)
    {
        uint64_t index = vpn_index(va, current);
        if (!ensure_next_table(table, index, &table))
        {
            return 0;
        }
    }

    uint64_t index = vpn_index(va, level);
    if (table[index] & PTE_V)
    {
        return 0;
    }

    /*
     * 第一版内核页表先用恒等映射把启动流程跑通，权限也先给成 R/W/X。
     * 后续拆 text/rodata/data、用户态地址空间和设备 MMIO 时，再收紧权限。
     */
    table[index] = phys_to_pte(pa, PTE_V | PTE_R | PTE_W | PTE_X | PTE_A | PTE_D);
    mapped_pages += page_size / PAGE_SIZE;
    return 1;
}

static int identity_map_range(uint64_t start, uint64_t size)
{
    /*
     * 恒等映射一段物理内存：VA == PA。
     *
     * 这里优先使用能覆盖当前地址的最大页大小。地址和剩余长度都满足 1GB 对齐时用
     * 1GB leaf；否则尝试 2MB leaf；最后退到 4KB leaf。这样映射大块 RAM 时不会为
     * 每个 4KB 页都建立最低级 PTE。
     */
    if (size == 0)
    {
        return 1;
    }

    uint64_t va = align_down(start, PAGE_SIZE);
    uint64_t end = align_up(start + size, PAGE_SIZE);
    mapped_ranges++;

    while (va < end)
    {
        uint64_t remaining = end - va;
        if (remaining >= PAGE_1G_SIZE && is_aligned(va, PAGE_1G_SIZE))
        {
            if (!map_leaf(va, va, PAGE_1G_SIZE, 2))
            {
                return 0;
            }
            va += PAGE_1G_SIZE;
            continue;
        }

        if (remaining >= PAGE_2M_SIZE && is_aligned(va, PAGE_2M_SIZE))
        {
            if (!map_leaf(va, va, PAGE_2M_SIZE, 1))
            {
                return 0;
            }
            va += PAGE_2M_SIZE;
            continue;
        }

        if (!map_leaf(va, va, PAGE_4K_SIZE, 0))
        {
            return 0;
        }
        va += PAGE_4K_SIZE;
    }

    return 1;
}

static int map_boot_info_ranges(const struct kernel_boot_info *boot_info)
{
    /*
     * 这些范围是打开页表前后都会立刻访问的启动材料，必须先映射：
     *
     * kernel image      : 当前正在执行的代码和数据
     * kernel stack      : 当前 sp 正在使用的栈
     * boot_info         : 入口参数本身
     * EFI memory map    : memory_state/page allocator 的来源
     * DTB               : 后续设备发现会用到
     */
    if (!identity_map_range(boot_info->kernel_phys_base, boot_info->kernel_size))
    {
        return 0;
    }
    if (!identity_map_range(boot_info->kernel_stack_phys, boot_info->kernel_stack_size))
    {
        return 0;
    }
    if (!identity_map_range(boot_info->boot_info_phys, boot_info->boot_info_size))
    {
        return 0;
    }
    if (!identity_map_range(boot_info->efi_memory_map_phys, boot_info->efi_memory_map_size))
    {
        return 0;
    }

    if ((boot_info->flags & KERNEL_BOOT_HAS_DTB) != 0)
    {
        if (!identity_map_range(boot_info->dtb_phys, boot_info->dtb_size))
        {
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
     * 确保新分配的页仍能被早期内核代码访问。
     */
    const struct boot_memory_state *memory = memory_state();
    for (uint64_t i = 0; i < memory->usable_range_count; i++)
    {
        uint64_t start = memory->usable_ranges[i].start;
        uint64_t size = memory->usable_ranges[i].end - memory->usable_ranges[i].start;
        if (!identity_map_range(start, size))
        {
            return 0;
        }
    }

    return 1;
}

static void write_satp(uint64_t value)
{
    /*
     * 写 satp 前后都执行 sfence.vma。前一次清掉旧地址转换缓存影响，后一次让新页表
     * 对后续取指和访存立即生效。
     */
    __asm__ volatile("sfence.vma zero, zero" ::: "memory");
    __asm__ volatile("csrw satp, %0" ::"r"(value) : "memory");
    __asm__ volatile("sfence.vma zero, zero" ::: "memory");
}

int early_vm_enable(const struct kernel_boot_info *boot_info)
{
    kernel_root_table = allocate_page_table();
    if (!kernel_root_table)
    {
        early_puts("Kernel page table allocation failed\r\n");
        return 0;
    }

    if (!map_boot_info_ranges(boot_info))
    {
        early_puts("Kernel boot ranges mapping failed\r\n");
        return 0;
    }
    if (!map_known_physical_memory())
    {
        early_puts("Kernel memory ranges mapping failed\r\n");
        return 0;
    }

    uint64_t root_phys = (uint64_t)(uintptr_t)kernel_root_table;
    uint64_t satp = (SV39_MODE << SATP_MODE_SHIFT) | (root_phys >> PAGE_4K_SHIFT);

    early_puts("Kernel page table prepared\r\n");
    early_print_field("root_table", root_phys);
    early_print_field("page_table_pages", page_table_pages);
    early_print_field("mapped_ranges", mapped_ranges);
    early_print_field("mapped_pages", mapped_pages);

    write_satp(satp);

    early_puts("Kernel Sv39 enabled\r\n");
    return 1;
}

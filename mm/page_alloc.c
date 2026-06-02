#include "early_log.h"
#include "boot_memory.h"
#include "page_alloc.h"

#define PAGE_SIZE 4096ULL

/*
 * 这里的 32 不是 32 个页表，也不是最多只能分配 32 次。
 *
 * 当前分配器用“空闲物理区间列表”管理内存，每个 free_range 描述一段连续的空闲
 * 物理页，比如 0x80203000..0x82bf5000。MAX_FREE_RANGES 只是这张区间表最多能保存
 * 多少段。QEMU/EDK2 当前整理出来只有 7 段 conventional usable ranges，所以 32
 * 对这一阶段足够宽松。
 *
 * 如果真实硬件给出更碎的 memory map，初始化会失败而不是静默丢内存。到那时可以
 * 调大这个常量，或者切换到 bitmap/Page[] 形式的正式页管理元数据。
 *
 * 这个分配器打算支撑内核启动后的继续推进，不只是 early_vm_enable() 的临时工具。
 * 但它还不是“整个 OS 生命周期的最终页分配器”：它没有 Page[] 元数据、引用计数、
 * 所有者、zone、NUMA 或更完整的 double-free 检查。后续正式物理内存管理会在这个
 * 接口基础上替换内部实现，或者把剩余 free ranges 导入新的元数据结构。
 */
#define MAX_FREE_RANGES 32

struct page_allocator_state {
    int ready;
    uint64_t free_range_count;
    uint64_t free_pages;
    struct phys_range free_ranges[MAX_FREE_RANGES];
};

static struct page_allocator_state allocator;

static uint64_t range_pages(const struct phys_range *range)
{
    return (range->end - range->start) / PAGE_SIZE;
}

static int range_is_valid(uint64_t start, uint64_t end)
{
    return start != 0 && end > start && (start % PAGE_SIZE) == 0 && (end % PAGE_SIZE) == 0;
}

static void reset_allocator(void)
{
    allocator.ready = 0;
    allocator.free_range_count = 0;
    allocator.free_pages = 0;

    for (uint64_t i = 0; i < MAX_FREE_RANGES; i++) {
        allocator.free_ranges[i].start = 0;
        allocator.free_ranges[i].end = 0;
    }
}

static void remove_range(uint64_t index)
{
    /*
     * 某段被完整分配后，从区间表里移除。这个数组很小，直接搬移比引入链表更清楚。
     */
    for (uint64_t i = index; i + 1 < allocator.free_range_count; i++) {
        allocator.free_ranges[i] = allocator.free_ranges[i + 1];
    }

    allocator.free_range_count--;
    allocator.free_ranges[allocator.free_range_count].start = 0;
    allocator.free_ranges[allocator.free_range_count].end = 0;
}

static void merge_around(uint64_t index)
{
    /*
     * free 时插回来的区间如果正好贴着前后空闲区间，就合并成更大的连续区间。
     * 这样分配器不会因为反复释放相邻页而越来越碎。
     */
    if (index > 0 &&
        allocator.free_ranges[index - 1].end == allocator.free_ranges[index].start) {
        allocator.free_ranges[index - 1].end = allocator.free_ranges[index].end;
        remove_range(index);
        index--;
    }

    if (index + 1 < allocator.free_range_count &&
        allocator.free_ranges[index].end == allocator.free_ranges[index + 1].start) {
        allocator.free_ranges[index].end = allocator.free_ranges[index + 1].end;
        remove_range(index + 1);
    }
}

static int insert_range(uint64_t start, uint64_t end)
{
    /*
     * 区间表保持按 start 升序排列，并拒绝重叠区间。页分配器的正确性主要靠这个
     * 不变量：任何物理页最多只会出现在一个 free_range 里。
     */
    if (!range_is_valid(start, end)) {
        return 0;
    }

    uint64_t pos = 0;
    while (pos < allocator.free_range_count && allocator.free_ranges[pos].start < start) {
        pos++;
    }

    if (pos > 0 && allocator.free_ranges[pos - 1].end > start) {
        return 0;
    }
    if (pos < allocator.free_range_count && end > allocator.free_ranges[pos].start) {
        return 0;
    }

    if (allocator.free_range_count >= MAX_FREE_RANGES) {
        return 0;
    }

    for (uint64_t i = allocator.free_range_count; i > pos; i--) {
        allocator.free_ranges[i] = allocator.free_ranges[i - 1];
    }

    allocator.free_ranges[pos].start = start;
    allocator.free_ranges[pos].end = end;
    allocator.free_range_count++;
    allocator.free_pages += (end - start) / PAGE_SIZE;
    merge_around(pos);
    return 1;
}

static void print_allocator_state(void)
{
    early_puts("Physical page allocator ready\r\n");
    early_print_field("free_ranges", allocator.free_range_count);
    early_print_field("free_pages", allocator.free_pages);
}

int page_allocator_init(void)
{
    reset_allocator();

    /*
     * memory_probe() 已经从 EFI memory map 中整理出当前先允许使用的 conventional
     * ranges，并把 kernel、boot_info、memory map、内核栈等启动保留页排除在外。
     * 这里把这些 ranges 复制进正式页分配器状态。复制之后，后续分配会修改 allocator
     * 自己的 free_ranges，不再修改 memory_state 里的启动快照。
     */
    const struct boot_memory_state *memory = memory_state();
    for (uint64_t i = 0; i < memory->usable_range_count; i++) {
        if (!insert_range(memory->usable_ranges[i].start, memory->usable_ranges[i].end)) {
            early_puts("Page allocator rejected usable range\r\n");
            return 0;
        }
    }

    allocator.ready = 1;
    print_allocator_state();
    return 1;
}

int page_allocator_ready(void)
{
    return allocator.ready;
}

uint64_t page_available_pages(void)
{
    return allocator.free_pages;
}

void *phys_alloc_pages(uint64_t pages)
{
    /*
     * first-fit：从第一段足够大的空闲区间头部切走 pages 页。
     * 这种策略不是最终性能方案，但足够支撑启动页表和基础内存管理继续推进。
     */
    if (!allocator.ready || pages == 0) {
        return 0;
    }

    uint64_t size = pages * PAGE_SIZE;
    if (size / PAGE_SIZE != pages) {
        return 0;
    }

    for (uint64_t i = 0; i < allocator.free_range_count; i++) {
        struct phys_range *range = &allocator.free_ranges[i];
        if (range_pages(range) < pages) {
            continue;
        }

        uint64_t addr = range->start;
        range->start += size;
        allocator.free_pages -= pages;

        if (range->start == range->end) {
            remove_range(i);
        }

        return (void *)(uintptr_t)addr;
    }

    return 0;
}

void phys_free_pages(void *addr, uint64_t pages)
{
    /*
     * 第一版 free 只做基本回收：插回区间表并尝试合并。它还没有记录 allocation
     * ownership，也不会捕获所有 double free 场景；这些检查适合等 Page[]/bitmap
     * 元数据接入后再加强。
     */
    if (!allocator.ready || !addr || pages == 0) {
        return;
    }

    uint64_t start = (uint64_t)(uintptr_t)addr;
    uint64_t size = pages * PAGE_SIZE;
    if (size / PAGE_SIZE != pages) {
        return;
    }

    if (insert_range(start, start + size)) {
        return;
    }

    early_puts("Page allocator ignored invalid free\r\n");
}

#include "early_log.h"
#include "boot_memory.h"
#include "page_alloc.h"

#define PAGE_SIZE 4096ULL
#define BITS_PER_WORD 64ULL

/*
 * 当前物理页分配器采用 bitmap + refcount。
 *
 * memory_probe() 先根据 EFI memory map 整理出 usable ranges，并排除内核镜像、
 * boot_info、最终 memory map、内核栈等启动保留页。page_allocator_init() 再把这些
 * usable ranges 导入 bitmap。
 *
 * 设计目标：
 *
 * - 不再依赖“空闲区间数量上限”，避免真实硬件 memory map 或反复 free 后把小数组
 *   撑爆；
 * - 能分配连续物理页，支撑页表页、kmalloc 后端、用户页第一版；
 * - 能用 refcount 判断基本非法释放，比如 double free、释放未分配页、越界释放；
 * - 先保持实现直接，不引入 buddy 的 order 拆分/合并复杂度。
 *
 * 仍然不是最终形态：
 *
 * - refcount 当前只记录“这页是否被分配”，还没有共享页/COW 语义；
 * - 分配连续多页时是线性扫描 bitmap，性能不是最终方案；
 * - 没有 zone、NUMA、页归属、页表页回收等完整内核能力。
 */
struct page_allocator_state
{
    int ready;

    /*
     * bitmap 覆盖 [base_pfn, base_pfn + page_count) 这段 PFN 范围。这里的范围可能
     * 包含不可用洞；洞里的页在 bitmap 中保持 used 状态，不会被分配出去。
     */
    uint64_t base_pfn;
    uint64_t page_count;

    /*
     * total_usable_pages 是从 memory_state 导入的可用页总数；free_pages 是当前还没
     * 分配出去的页数。metadata 自身也从 usable pages 里扣掉。
     */
    uint64_t total_usable_pages;
    uint64_t free_pages;

    /*
     * 元数据本身也放在物理内存里。初始化时从第一段足够大的 usable range 头部切出
     * metadata_pages 页，并立刻在 bitmap/refcount 里标成已占用。
     */
    uint64_t metadata_phys;
    uint64_t metadata_pages;
    uint64_t bitmap_words;

    /*
     * free_bitmap: bit=1 表示空闲，bit=0 表示不可用或已分配。
     * refcounts  : 0 表示未分配，非 0 表示已分配。当前只用 0/1。
     */
    uint64_t *free_bitmap;
    uint16_t *refcounts;
};

static struct page_allocator_state allocator;

static uint64_t align_up(uint64_t value, uint64_t align)
{
    return (value + align - 1) & ~(align - 1);
}

static uint64_t pages_for_size(uint64_t size)
{
    return (size + PAGE_SIZE - 1) / PAGE_SIZE;
}

static uint64_t range_pages(const struct phys_range *range)
{
    return (range->end - range->start) / PAGE_SIZE;
}

static void zero_bytes(void *ptr, uint64_t size)
{
    uint8_t *bytes = (uint8_t *)ptr;
    for (uint64_t i = 0; i < size; i++)
    {
        bytes[i] = 0;
    }
}

static uint64_t pfn_to_index(uint64_t pfn)
{
    return pfn - allocator.base_pfn;
}

static uint64_t index_to_pfn(uint64_t index)
{
    return allocator.base_pfn + index;
}

static int index_is_valid(uint64_t index)
{
    return index < allocator.page_count;
}

static uint64_t bitmap_word(uint64_t index)
{
    return index / BITS_PER_WORD;
}

static uint64_t bitmap_mask(uint64_t index)
{
    return 1ULL << (index % BITS_PER_WORD);
}

static int page_is_free(uint64_t index)
{
    return (allocator.free_bitmap[bitmap_word(index)] & bitmap_mask(index)) != 0;
}

static void mark_page_free(uint64_t index)
{
    allocator.free_bitmap[bitmap_word(index)] |= bitmap_mask(index);
}

static void mark_page_used(uint64_t index)
{
    allocator.free_bitmap[bitmap_word(index)] &= ~bitmap_mask(index);
}

static void reset_allocator(void)
{
    allocator.ready = 0;
    allocator.base_pfn = 0;
    allocator.page_count = 0;
    allocator.total_usable_pages = 0;
    allocator.free_pages = 0;
    allocator.metadata_phys = 0;
    allocator.metadata_pages = 0;
    allocator.bitmap_words = 0;
    allocator.free_bitmap = 0;
    allocator.refcounts = 0;
}

static int find_pfn_span(const struct boot_memory_state *memory)
{
    /*
     * bitmap 需要一个连续的索引空间。这里找出所有 usable ranges 覆盖到的最小/最大
     * PFN，形成 [base_pfn, base_pfn + page_count)。中间如果有洞，后续不会被
     * mark_usable_range() 标为空闲，因此不会被分配。
     */
    uint64_t min_pfn = UINT64_MAX;
    uint64_t max_pfn = 0;

    for (uint64_t i = 0; i < memory->usable_range_count; i++)
    {
        const struct phys_range *range = &memory->usable_ranges[i];
        if (range->start == 0 || range->end <= range->start)
        {
            continue;
        }

        uint64_t start_pfn = range->start / PAGE_SIZE;
        uint64_t end_pfn = range->end / PAGE_SIZE;
        if (start_pfn < min_pfn)
        {
            min_pfn = start_pfn;
        }
        if (end_pfn > max_pfn)
        {
            max_pfn = end_pfn;
        }
    }

    if (min_pfn == UINT64_MAX || max_pfn <= min_pfn)
    {
        return 0;
    }

    allocator.base_pfn = min_pfn;
    allocator.page_count = max_pfn - min_pfn;
    return 1;
}

static uint64_t metadata_size_bytes(void)
{
    /*
     * 元数据大小取决于 PFN 跨度，而不是 usable 页数。这样可以用 index = pfn -
     * base_pfn 直接定位；代价是如果物理地址空间很稀疏，bitmap/refcount 会多覆盖
     * 一些洞。对当前 QEMU/板子规模这比复杂 sparse mem 更合适。
     */
    allocator.bitmap_words = align_up(allocator.page_count, BITS_PER_WORD) / BITS_PER_WORD;

    /*
     * 这些乘法结果会决定后面实际保留多少元数据空间。先判断乘法是否会超过
     * uint64_t 上限，避免回绕成一个很小的值，导致 bitmap/refcount 写出预留区。
     */
    if (allocator.bitmap_words > UINT64_MAX / sizeof(uint64_t)) {
        return 0;
    }
    uint64_t bitmap_bytes = allocator.bitmap_words * sizeof(uint64_t);

    if (allocator.page_count > UINT64_MAX / sizeof(uint16_t)) {
        return 0;
    }
    uint64_t refcount_bytes = allocator.page_count * sizeof(uint16_t);

    if (bitmap_bytes + refcount_bytes < bitmap_bytes)
    {
        return 0;
    }

    return bitmap_bytes + refcount_bytes;
}

static int reserve_metadata_storage(
    const struct boot_memory_state *memory,
    uint64_t metadata_bytes)
{
    /*
     * 这里还不能调用 phys_alloc_pages()，因为页分配器自己还没初始化完成。
     * 所以直接从 boot_memory_state 的 usable ranges 中找一段足够大的物理内存，
     * 作为 bitmap/refcount 存放区。真正把这段页标为已占用，会在 bitmap 建好后由
     * reserve_allocated_range() 完成。
     */
    allocator.metadata_pages = pages_for_size(metadata_bytes);
    if (allocator.metadata_pages > UINT64_MAX / PAGE_SIZE) {
        return 0;
    }
    uint64_t metadata_size = allocator.metadata_pages * PAGE_SIZE;

    for (uint64_t i = 0; i < memory->usable_range_count; i++)
    {
        const struct phys_range *range = &memory->usable_ranges[i];
        if (range_pages(range) < allocator.metadata_pages)
        {
            continue;
        }

        allocator.metadata_phys = range->start;
        allocator.free_bitmap = (uint64_t *)(uintptr_t)allocator.metadata_phys;
        allocator.refcounts = (uint16_t *)(uintptr_t)(allocator.metadata_phys + allocator.bitmap_words * sizeof(uint64_t));
        zero_bytes((void *)(uintptr_t)allocator.metadata_phys, metadata_size);
        return 1;
    }

    return 0;
}

static void mark_usable_range(const struct phys_range *range)
{
    /*
     * 把 memory_probe() 认为可用的页标成 free。没有出现在 usable_ranges 里的 PFN
     * 保持 bit=0，即“不可分配”。因此 bitmap 可以覆盖洞，但不会把洞误当内存。
     */
    uint64_t start_pfn = range->start / PAGE_SIZE;
    uint64_t end_pfn = range->end / PAGE_SIZE;

    for (uint64_t pfn = start_pfn; pfn < end_pfn; pfn++)
    {
        uint64_t index = pfn_to_index(pfn);
        if (!index_is_valid(index))
        {
            continue;
        }

        if (!page_is_free(index))
        {
            mark_page_free(index);
            allocator.total_usable_pages++;
            allocator.free_pages++;
        }
    }
}

static int reserve_page_index(uint64_t index)
{
    /*
     * 把一个当前空闲的页转成已占用。metadata 预留、未来固定保留页都可以走这个
     * helper。这里要求页必须已经在 bitmap 中是 free，避免重复 reserve。
     */
    if (!index_is_valid(index) || !page_is_free(index))
    {
        return 0;
    }

    mark_page_used(index);
    allocator.refcounts[index] = 1;
    allocator.free_pages--;
    return 1;
}

static int reserve_allocated_range(uint64_t phys, uint64_t pages)
{
    uint64_t start_pfn = phys / PAGE_SIZE;

    for (uint64_t i = 0; i < pages; i++)
    {
        uint64_t index = pfn_to_index(start_pfn + i);
        if (!reserve_page_index(index))
        {
            return 0;
        }
    }

    return 1;
}

static int find_free_run(uint64_t pages, uint64_t *start_index)
{
    /*
     * 查找 pages 个连续空闲页。第一版采用线性 first-fit 扫描，逻辑简单且便于调试。
     * 后续如果 fork/exec 和文件缓存带来更高分配压力，可以把内部替换成 buddy，
     * 对外 phys_alloc_pages()/phys_free_pages() 接口不必变。
     */
    uint64_t run_start = 0;
    uint64_t run_pages = 0;

    for (uint64_t i = 0; i < allocator.page_count; i++)
    {
        if (!page_is_free(i))
        {
            run_pages = 0;
            continue;
        }

        if (run_pages == 0)
        {
            run_start = i;
        }
        run_pages++;

        if (run_pages == pages)
        {
            *start_index = run_start;
            return 1;
        }
    }

    return 0;
}

static int allocated_range_is_valid(uint64_t start_index, uint64_t pages)
{
    /*
     * free 前的基本防线：被释放的每一页都必须在管理范围内、当前不是 free、
     * refcount 非 0。这样能挡住最常见的 double free、释放未分配页、越界释放。
     */
    if (!index_is_valid(start_index) || pages == 0)
    {
        return 0;
    }
    if (pages > allocator.page_count - start_index)
    {
        return 0;
    }

    for (uint64_t i = 0; i < pages; i++)
    {
        uint64_t index = start_index + i;
        if (page_is_free(index) || allocator.refcounts[index] == 0)
        {
            return 0;
        }
    }

    return 1;
}

static void print_allocator_state(void)
{
    early_puts("Physical page allocator ready\r\n");
    early_print_field("base_pfn", allocator.base_pfn);
    early_print_field("managed_pages", allocator.page_count);
    early_print_field("usable_pages", allocator.total_usable_pages);
    early_print_field("free_pages", allocator.free_pages);
    early_print_field("metadata_phys", allocator.metadata_phys);
    early_print_field("metadata_pages", allocator.metadata_pages);
}

int page_allocator_init(void)
{
    reset_allocator();

    /*
     * 初始化顺序：
     *
     * 1. 根据 usable ranges 计算 PFN 覆盖范围；
     * 2. 计算 bitmap/refcount 需要多少字节；
     * 3. 从 usable memory 中找地方存放元数据并清零；
     * 4. 把所有 usable ranges 标为空闲；
     * 5. 再把 metadata 自己占用的页标回已占用。
     */
    const struct boot_memory_state *memory = memory_state();
    if (!find_pfn_span(memory))
    {
        early_puts("Page allocator rejected empty memory ranges\r\n");
        return 0;
    }

    uint64_t metadata_bytes = metadata_size_bytes();
    if (metadata_bytes == 0 || !reserve_metadata_storage(memory, metadata_bytes))
    {
        early_puts("Page allocator metadata allocation failed\r\n");
        return 0;
    }

    for (uint64_t i = 0; i < memory->usable_range_count; i++)
    {
        mark_usable_range(&memory->usable_ranges[i]);
    }

    if (!reserve_allocated_range(allocator.metadata_phys, allocator.metadata_pages))
    {
        early_puts("Page allocator metadata reservation failed\r\n");
        return 0;
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

uint32_t phys_page_state(void *addr)
{
    if (!allocator.ready || !addr)
    {
        return PHYS_PAGE_INVALID;
    }

    uint64_t phys = (uint64_t)(uintptr_t)addr;
    if ((phys % PAGE_SIZE) != 0)
    {
        return PHYS_PAGE_INVALID;
    }

    uint64_t pfn = phys / PAGE_SIZE;
    if (pfn < allocator.base_pfn)
    {
        return PHYS_PAGE_INVALID;
    }

    uint64_t index = pfn_to_index(pfn);
    if (!index_is_valid(index))
    {
        return PHYS_PAGE_INVALID;
    }

    if (page_is_free(index))
    {
        return PHYS_PAGE_FREE;
    }

    if (allocator.refcounts[index] != 0)
    {
        return PHYS_PAGE_ALLOCATED;
    }

    return PHYS_PAGE_RESERVED;
}

void *phys_alloc_pages(uint64_t pages)
{
    if (!allocator.ready || pages == 0)
    {
        return 0;
    }
    if (pages > allocator.free_pages)
    {
        return 0;
    }

    uint64_t start_index = 0;
    if (!find_free_run(pages, &start_index))
    {
        return 0;
    }

    for (uint64_t i = 0; i < pages; i++)
    {
        uint64_t index = start_index + i;
        mark_page_used(index);
        allocator.refcounts[index] = 1;
    }
    allocator.free_pages -= pages;

    uint64_t phys = index_to_pfn(start_index) * PAGE_SIZE;
    return (void *)(uintptr_t)phys;
}

void phys_free_pages(void *addr, uint64_t pages)
{
    if (!allocator.ready || !addr || pages == 0)
    {
        return;
    }

    uint64_t start = (uint64_t)(uintptr_t)addr;
    if ((start % PAGE_SIZE) != 0)
    {
        early_puts("Page allocator ignored unaligned free\r\n");
        return;
    }

    uint64_t start_pfn = start / PAGE_SIZE;
    if (start_pfn < allocator.base_pfn)
    {
        early_puts("Page allocator ignored out-of-range free\r\n");
        return;
    }

    uint64_t start_index = pfn_to_index(start_pfn);
    if (!allocated_range_is_valid(start_index, pages))
    {
        early_puts("Page allocator ignored invalid free\r\n");
        return;
    }

    for (uint64_t i = 0; i < pages; i++)
    {
        uint64_t index = start_index + i;
        allocator.refcounts[index] = 0;
        mark_page_free(index);
    }
    allocator.free_pages += pages;
}

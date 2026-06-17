#include "vm.h"

#include "align.h"
#include "page_alloc.h"
#include "string.h"

#define PTE_COUNT 512ULL

#define SV39_MODE 8ULL
#define SATP_MODE_SHIFT 60

/*
 * RISC-V Sv39 PTE 低 10 位保存权限和状态位，高位保存物理页号 PPN。
 *
 * V(valid) 表示这条 PTE 有效。R/W/X 同时全为 0 时，PTE 指向下一级页表；
 * 只要 R/W/X 至少有一位为 1，它就是叶子映射。
 *
 * A(accessed) 表示“这个页已经被访问过”，D(dirty) 表示“这个页已经被写过”。它们
 * 不是 cache 那样由硬件自动清零的状态位；通常是硬件或 OS 置 1，OS 在需要统计
 * 最近访问/写入情况时再主动清 0。RISC-V 允许两种实现：有的硬件会自动把 A/D 置
 * 1，有的硬件会在 A/D 缺失时触发 page fault，让 OS 在异常处理里手动补位。
 *
 * 当前还没有 page fault handler，所以创建映射时预先置 A，并对可写页预先置 D。
 * 正式缺页异常接入后，可以再改成懒维护。
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

static uint64_t page_size_for_level(int level)
{
    if (level == 2)
    {
        return PAGE_1G_SIZE;
    }
    if (level == 1)
    {
        return PAGE_2M_SIZE;
    }
    return PAGE_4K_SIZE;
}

static int pte_is_leaf(pte_t pte)
{
    return (pte & (PTE_R | PTE_W | PTE_X)) != 0;
}

static uint64_t vm_flags_to_pte(uint64_t flags)
{
    uint64_t pte_flags = PTE_A;

    if (flags & VM_MAP_READ)
    {
        pte_flags |= PTE_R;
    }
    if (flags & VM_MAP_WRITE)
    {
        pte_flags |= PTE_W | PTE_D;
    }
    if (flags & VM_MAP_EXEC)
    {
        pte_flags |= PTE_X;
    }

    return pte_flags;
}

static uint64_t pte_flags_to_vm(pte_t pte)
{
    uint64_t flags = 0;

    if (pte & PTE_R)
    {
        flags |= VM_MAP_READ;
    }
    if (pte & PTE_W)
    {
        flags |= VM_MAP_WRITE;
    }
    if (pte & PTE_X)
    {
        flags |= VM_MAP_EXEC;
    }

    return flags;
}

static int vm_flags_are_valid(uint64_t flags)
{
    // 检查未知flag
    if ((flags & ~(VM_MAP_READ | VM_MAP_WRITE | VM_MAP_EXEC)) != 0)
    {
        return 0;
    }
    // 检查flags=0
    if ((flags & (VM_MAP_READ | VM_MAP_WRITE | VM_MAP_EXEC)) == 0)
    {
        return 0;
    }

    /*
     * RISC-V 页表把 W=1 且 R=0 的叶子 PTE 定义为保留组合。写映射必须同时可读，
     * 否则后续访问会触发 page fault，甚至在不同实现上表现不一致。
     */
    if ((flags & VM_MAP_WRITE) != 0 && (flags & VM_MAP_READ) == 0)
    {
        return 0;
    }

    return 1;
}

static pte_t *allocate_page_table(struct vm_space *space)
{
    void *page = phys_alloc_pages(1);
    if (!page)
    {
        return 0;
    }

    /*
     * 当前内核仍保持物理地址恒等映射，所以物理页地址可以直接当指针清零。
     * 后面引入非恒等 direct map 后，这里需要改成 phys_to_virt()。
     */
    memzero(page, VM_PAGE_SIZE);
    space->page_table_pages++;
    return (pte_t *)page;
}

static int ensure_next_table(struct vm_space *space, pte_t *table,
                             uint64_t index, pte_t **next)
{
    /*
     * map_leaf() 从根页表一路走到目标 level。中间层 PTE 必须指向“下一张页表”。
     * ensure_next_table() 做的就是：
     *
     * 1. 如果 table[index] 已经是一个非叶子 PTE，就取出它指向的下一级页表；
     * 2. 如果 table[index] 为空，就分配一页新的页表页，写成非叶子 PTE；
     * 3. 如果 table[index] 已经是叶子映射，说明这里不能再往下挂页表，拒绝。
     */
    pte_t pte = table[index];
    if (pte & PTE_V)
    {
        if (pte_is_leaf(pte))
        {
            return 0;
        }

        *next = (pte_t *)(uintptr_t)pte_to_phys(pte);
        return 1;
    }

    pte_t *new_table = allocate_page_table(space);
    if (!new_table)
    {
        return 0;
    }

    table[index] = phys_to_pte((uint64_t)(uintptr_t)new_table, PTE_V);
    *next = new_table;
    return 1;
}

static int map_leaf(struct vm_space *space, uint64_t va, uint64_t pa,
                    uint64_t page_size, int level, uint64_t flags)
{
    /*
     * 在指定 level 写入叶子 PTE：
     *
     * level 2 leaf: 1GB 映射
     * level 1 leaf: 2MB 映射
     * level 0 leaf: 4KB 映射
     */
    pte_t *table = space->root_table;

    for (int current = 2; current > level; current--)
    {
        uint64_t index = vpn_index(va, current);
        if (!ensure_next_table(space, table, index, &table))
        {
            return 0;
        }
    }

    uint64_t index = vpn_index(va, level);
    if (table[index] & PTE_V)
    {
        return 0;
    }

    table[index] = phys_to_pte(pa, PTE_V | vm_flags_to_pte(flags));
    space->mapped_pages += page_size / VM_PAGE_SIZE;
    return 1;
}

static int unmap_one_leaf(struct vm_space *space, uint64_t va,
                          uint64_t *leaf_size)
{
    /*
     * 从根页表沿着 va 对应的索引向下走，找到覆盖 va 的那一个“叶子 PTE”，然后把
     * 这条 PTE 清零。它只卸载一片叶子映射，不释放被映射的物理页，也不回收中间
     * 页表页。
     *
     * 注意：叶子可能在不同层级：
     *
     * - level 2 leaf 覆盖 1GB
     * - level 1 leaf 覆盖 2MB
     * - level 0 leaf 覆盖 4KB
     *
     * 因此函数通过 leaf_size 告诉调用者“刚刚卸掉了多大一段”。外层
     * vm_unmap_range() 再据此推进 va，继续卸下一片叶子。
     */
    pte_t *table = space->root_table;

    for (int level = 2; level >= 0; level--)
    {
        uint64_t index = vpn_index(va, level);
        pte_t pte = table[index];

        if ((pte & PTE_V) == 0)
        {
            return 0;
        }

        if (pte_is_leaf(pte))
        {
            uint64_t size = page_size_for_level(level);
            if (!is_aligned(va, size))
            {
                /*
                 * 如果 va
                 * 落在一个大页中间，第一版先拒绝拆分。比如调用者只想卸载 1GB
                 * 映射里的某个 4KB 页，就需要先把 1GB leaf 拆成下一级页表。
                 * 这个能力后面做正式 VM/用户页表时再补。
                 */
                return 0;
            }
            table[index] = 0;
            space->mapped_pages -= size / VM_PAGE_SIZE;
            *leaf_size = size;
            return 1;
        }

        table = (pte_t *)(uintptr_t)pte_to_phys(pte);
    }

    return 0;
}

static void rollback_mapped_prefix(struct vm_space *space, uint64_t start_va,
                                   uint64_t end_va,
                                   uint64_t mapped_pages_before)
{
    /*
     * map_range_inner() 失败时，撤销本次已经写入的 leaf PTE，让对外接口保持
     * “成功全映射，失败不留下部分 leaf 映射”的语义。
     *
     * 这里不回收映射过程中按需创建的中间页表页。空页表页不会暴露额外 VA->PA
     * 映射，只是暂时多占几页内存。
     *
     * TODO: 等页表页引用计数、递归释放或路径复制事务稳定后，把失败路径里的中间
     * 页表页也精确回收掉。
     */
    while (start_va < end_va)
    {
        uint64_t leaf_size = 0;
        if (!unmap_one_leaf(space, start_va, &leaf_size))
        {
            break;
        }
        start_va += leaf_size;
    }

    space->mapped_pages = mapped_pages_before;
    vm_flush_all();
}

static int map_range_inner(struct vm_space *space, uint64_t va, uint64_t pa,
                           uint64_t size, uint64_t flags)
{
    /*
     * vm_map_range() 负责把外部传入的地址/长度扩成 4KB 对齐范围；
     * map_range_inner() 接手后，假设 va/pa 已经页对齐，并真正把这段范围拆成
     * 若干个叶子 PTE 写入页表。
     *
     * 拆分策略是“能用大页就用大页”：
     *
     * - va、pa、剩余长度都满足 1GB 对齐/大小时，写 level-2 leaf；
     * - 否则满足 2MB 条件时，写 level-1 leaf；
     * - 最后退回 4KB level-0 leaf。
     *
     * 这里的循环不是在分配物理内存。pa 是调用者已经决定好的物理地址，本函数只
     * 负责建立 va -> pa 的页表映射；中间页表页不够时，map_leaf() 会通过
     * ensure_next_table() 分配页表页。
     */
    if (size == 0)
    {
        return 1;
    }

    if ((va % VM_PAGE_SIZE) != 0 || (pa % VM_PAGE_SIZE) != 0)
    {
        return 0;
    }

    uint64_t end = va + size;
    if (end < va)
    {
        return 0;
    }

    uint64_t start_va = va;
    uint64_t mapped_pages_before = space->mapped_pages;

    while (va < end)
    {
        uint64_t remaining = end - va;

        if (remaining >= PAGE_1G_SIZE && is_aligned(va, PAGE_1G_SIZE) &&
            is_aligned(pa, PAGE_1G_SIZE))
        {
            if (!map_leaf(space, va, pa, PAGE_1G_SIZE, 2, flags))
            {
                rollback_mapped_prefix(space, start_va, va,
                                       mapped_pages_before);
                return 0;
            }
            va += PAGE_1G_SIZE;
            pa += PAGE_1G_SIZE;
            continue;
        }

        if (remaining >= PAGE_2M_SIZE && is_aligned(va, PAGE_2M_SIZE) &&
            is_aligned(pa, PAGE_2M_SIZE))
        {
            if (!map_leaf(space, va, pa, PAGE_2M_SIZE, 1, flags))
            {
                rollback_mapped_prefix(space, start_va, va,
                                       mapped_pages_before);
                return 0;
            }
            va += PAGE_2M_SIZE;
            pa += PAGE_2M_SIZE;
            continue;
        }

        if (!map_leaf(space, va, pa, PAGE_4K_SIZE, 0, flags))
        {
            rollback_mapped_prefix(space, start_va, va, mapped_pages_before);
            return 0;
        }
        va += PAGE_4K_SIZE;
        pa += PAGE_4K_SIZE;
    }

    return 1;
}

void vm_space_init(struct vm_space *space)
{
    space->root_table = 0;
    space->page_table_pages = 0;
    space->mapped_ranges = 0;
    space->mapped_pages = 0;
}

int vm_space_create(struct vm_space *space)
{
    /*
     * 创建一张空页表：当前只分配根页表页。后续 vm_map_range() 需要中间页表时，
     * 再按需分配 level-1 / level-0 页表页。
     */
    vm_space_init(space);
    space->root_table = allocate_page_table(space);
    return space->root_table != 0;
}

int vm_map_range(struct vm_space *space, uint64_t va, uint64_t pa,
                 uint64_t size, uint64_t flags)
{
    /*
     * 对外映射接口。
     *
     * 调用者可以传入不完全页对齐的起点和长度，比如映射 DTB 的真实大小。页表只能
     * 以页为单位工作，所以这里会把 va/pa 向下对齐，把 size 向上扩到覆盖完整页。
     *
     * 当前版本要求 va 和 pa 的页内偏移一致；否则对齐后会改变“va + offset”对应的
     * 物理位置。启动期现在都做恒等映射，满足这个条件。后面如果需要映射任意
     * va->pa，可以把这个检查放宽并更严格地处理 offset。
     */
    if (!space || !space->root_table)
    {
        return 0;
    }
    if (size == 0)
    {
        return 1;
    }
    if (!vm_flags_are_valid(flags))
    {
        return 0;
    }

    uint64_t aligned_va = align_down(va, VM_PAGE_SIZE);
    uint64_t offset = va - aligned_va;
    uint64_t aligned_pa = align_down(pa, VM_PAGE_SIZE);
    if ((pa - aligned_pa) != offset)
    {
        return 0;
    }
    if (size > UINT64_MAX - offset)
    {
        return 0;
    }

    uint64_t requested_size = size + offset;
    if (requested_size > UINT64_MAX - (VM_PAGE_SIZE - 1))
    {
        return 0;
    }

    uint64_t aligned_size = align_up(requested_size, VM_PAGE_SIZE);
    if (!map_range_inner(space, aligned_va, aligned_pa, aligned_size, flags))
    {
        return 0;
    }

    space->mapped_ranges++;
    return 1;
}

int vm_identity_map(struct vm_space *space, uint64_t start, uint64_t size,
                    uint64_t flags)
{
    return vm_map_range(space, start, start, size, flags);
}

int vm_unmap_range(struct vm_space *space, uint64_t va, uint64_t size)
{
    /*
     * 对外解除映射接口。
     *
     * 这里的“unmap”只清页表项，不释放物理页。原因是页表不知道这段物理页归谁：
     * 它可能是内核镜像、MMIO、DTB、用户页，也可能是共享页。释放物理页应该由更高
     * 层的所有者决定。
     *
     * 第一版要求 va 页对齐，并且不支持从大页中间拆一小段。这个限制可以避免误删
     * 超过调用者预期的映射。
     */
    if (!space || !space->root_table || size == 0 || (va % VM_PAGE_SIZE) != 0)
    {
        return 0;
    }

    uint64_t end = va + align_up(size, VM_PAGE_SIZE);
    if (end < va)
    {
        return 0;
    }

    while (va < end)
    {
        uint64_t leaf_size = 0;
        if (!unmap_one_leaf(space, va, &leaf_size))
        {
            return 0;
        }
        va += leaf_size;
    }

    __asm__ volatile("sfence.vma zero, zero" ::: "memory");
    return 1;
}

int vm_query(const struct vm_space *space, uint64_t va,
             struct vm_mapping *mapping)
{
    if (!space || !space->root_table || !mapping)
    {
        return 0;
    }

    pte_t *table = space->root_table;
    for (int level = 2; level >= 0; level--)
    {
        uint64_t index = vpn_index(va, level);
        pte_t pte = table[index];

        if ((pte & PTE_V) == 0)
        {
            return 0;
        }

        if (pte_is_leaf(pte))
        {
            uint64_t leaf_size = page_size_for_level(level);
            uint64_t offset = va & (leaf_size - 1);

            mapping->pa = pte_to_phys(pte) + offset;
            mapping->leaf_size = leaf_size;
            mapping->flags = pte_flags_to_vm(pte);
            return 1;
        }

        table = (pte_t *)(uintptr_t)pte_to_phys(pte);
    }

    return 0;
}

void vm_activate_sv39(const struct vm_space *space)
{
    uint64_t root_phys = (uint64_t)(uintptr_t)space->root_table;
    uint64_t satp =
        (SV39_MODE << SATP_MODE_SHIFT) | (root_phys >> PAGE_4K_SHIFT);

    /*
     * 写 satp 前后都执行
     * sfence.vma。前一次清掉旧地址转换缓存影响，后一次让新页表
     * 对后续取指和访存立即生效。
     */
    vm_flush_all();
    __asm__ volatile("csrw satp, %0" ::"r"(satp) : "memory");
    vm_flush_all();
}

void vm_flush_all(void)
{
    __asm__ volatile("sfence.vma zero, zero" ::: "memory");
}

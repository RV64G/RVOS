#ifndef MM_VM_H
#define MM_VM_H

#include <stdint.h>

#define VM_PAGE_SIZE 4096ULL

#define VM_MAP_READ  (1ULL << 0)
#define VM_MAP_WRITE (1ULL << 1)
#define VM_MAP_EXEC  (1ULL << 2)

typedef uint64_t pte_t;

struct vm_space {
    pte_t *root_table;
    uint64_t page_table_pages;
    uint64_t mapped_ranges;
    uint64_t mapped_pages;
};

/**
 * 清空 vm_space 结构本身，不分配页表页。
 */
void vm_space_init(struct vm_space *space);

/**
 * 创建一张空 Sv39 页表。
 *
 * 当前只分配 root table；后续 vm_map_range() 在需要下一级页表时再按需分配。
 */
int vm_space_create(struct vm_space *space);

/**
 * 把一段虚拟地址映射到一段物理地址。
 *
 * 当前基础接口建议按 4KB 页对齐使用。它只写页表，不分配被映射的物理页；调用者
 * 必须先通过 phys_alloc_pages() 或其它方式拿到 pa。flags 使用 VM_MAP_*。
 */
int vm_map_range(
    struct vm_space *space,
    uint64_t va,
    uint64_t pa,
    uint64_t size,
    uint64_t flags
);

/**
 * 建立 VA == PA 的恒等映射。
 *
 * 当前内核早期和 direct-map 雏形主要用这个接口，确保打开 Sv39 后仍能用物理地址
 * 直接访问内存。
 */
int vm_identity_map(struct vm_space *space, uint64_t start, uint64_t size, uint64_t flags);

/**
 * 清除一段虚拟地址映射。
 *
 * 只清 PTE，不释放物理页。物理页归属由更高层决定，比如 kmalloc、用户地址空间、
 * 文件缓存或 MMIO 映射。
 */
int vm_unmap_range(struct vm_space *space, uint64_t va, uint64_t size);

/**
 * 把给定页表写入 satp 并启用 Sv39。
 */
void vm_activate_sv39(const struct vm_space *space);

#endif

#ifndef MM_VM_H
#define MM_VM_H

#include <stdint.h>

#define VM_PAGE_SIZE 4096ULL

#define VM_MAP_READ  (1ULL << 0)
#define VM_MAP_WRITE (1ULL << 1)
#define VM_MAP_EXEC  (1ULL << 2)
/** 允许 U-mode 访问这段映射。没有这个 flag 时映射仅供 S-mode 内核使用。 */
#define VM_MAP_USER  (1ULL << 3)

typedef uint64_t pte_t;

struct vm_space {
    pte_t *root_table;
    uint64_t page_table_pages;
    uint64_t mapped_ranges;
    uint64_t mapped_pages;
};

struct vm_mapping {
    /** 输入 VA 当前对应的物理地址，包含页内偏移。 */
    uint64_t pa;

    /** 覆盖该 VA 的叶子 PTE 大小，可能是 4KB、2MB 或 1GB。 */
    uint64_t leaf_size;

    /** VM_MAP_* 权限组合。 */
    uint64_t flags;
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
 * 把 src 中的 S-mode 映射复制到 dst。
 *
 * 这个接口用于创建用户地址空间的第一版骨架：用户页表需要能在 trap 后继续执行内核
 * 代码、访问内核数据、MMIO 和 direct map，但不应该继承已有的 U-mode 映射。
 */
int vm_copy_kernel_mappings(struct vm_space *dst, const struct vm_space *src);

/**
 * 把一段虚拟地址映射到一段物理地址。
 *
 * 当前基础接口建议按 4KB 页对齐使用。它只写页表，不分配被映射的物理页；调用者
 * 必须先通过 phys_alloc_pages() 或其它方式拿到 pa。flags 使用 VM_MAP_*。
 *
 * RISC-V Sv39 不允许 write-only 叶子 PTE；传入 VM_MAP_WRITE 时必须同时传入
 * VM_MAP_READ。
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
 * 查询一个虚拟地址当前对应的叶子映射。
 *
 * 成功返回 1，并填充 mapping；未映射或遇到无效页表返回 0。这个接口只读页表，
 * 不分配页表页，也不改变 TLB。
 */
int vm_query(const struct vm_space *space, uint64_t va, struct vm_mapping *mapping);

/**
 * 把给定页表写入 satp 并启用 Sv39。
 */
void vm_activate_sv39(const struct vm_space *space);

/**
 * 刷新当前 hart 的全部地址转换缓存。
 *
 * 启用 MMU 后如果继续修改当前正在使用的页表，需要在修改后执行一次刷新，让新的
 * PTE 对后续访存可见。
 */
void vm_flush_all(void);

#endif

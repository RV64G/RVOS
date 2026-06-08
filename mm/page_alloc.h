#ifndef MM_PAGE_ALLOC_H
#define MM_PAGE_ALLOC_H

#include <stdint.h>

#define PHYS_PAGE_INVALID   0U
#define PHYS_PAGE_FREE      1U
#define PHYS_PAGE_ALLOCATED 2U
#define PHYS_PAGE_RESERVED  3U

/**
 * 从启动阶段整理出的可用物理内存初始化页分配器。
 *
 * 初始化时会从可用物理页中预留一段空间保存 bitmap/refcount 元数据。完成后，
 * 后续页表页、kmalloc 后端、用户页第一版都可以通过 phys_alloc_pages() 申请页。
 */
int page_allocator_init(void);

/**
 * 返回页分配器是否已经初始化完成。
 */
int page_allocator_ready(void);

/**
 * 返回当前还可分配的 4KB 物理页数量。
 */
uint64_t page_available_pages(void);

/**
 * 查询一个 4KB 物理页在页分配器中的状态。
 *
 * addr 必须是页对齐物理地址。返回值：
 *
 * - PHYS_PAGE_INVALID：页分配器未就绪、地址未对齐或超出管理范围。
 * - PHYS_PAGE_FREE：当前可由 phys_alloc_pages() 分配。
 * - PHYS_PAGE_ALLOCATED：当前已由页分配器分配或保留给元数据。
 * - PHYS_PAGE_RESERVED：位于管理 PFN 跨度内，但不是可分配页，通常是 memory map 洞。
 */
uint32_t phys_page_state(void *addr);

/**
 * 分配 pages 个连续 4KB 物理页。
 *
 * 返回值是物理地址。当前内核仍保持恒等映射，所以早期代码可以直接把它当指针用。
 * 如果没有足够连续空闲页，返回 0。
 */
void *phys_alloc_pages(uint64_t pages);

/**
 * 释放 pages 个连续 4KB 物理页。
 *
 * 这里只回收物理页，不清零页内容。释放未分配页、重复释放、未对齐地址或越界地址
 * 会被拒绝并通过 early_log 记录。
 */
void phys_free_pages(void *addr, uint64_t pages);

#endif

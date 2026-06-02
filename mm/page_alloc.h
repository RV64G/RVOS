#ifndef MM_PAGE_ALLOC_H
#define MM_PAGE_ALLOC_H

#include <stdint.h>

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

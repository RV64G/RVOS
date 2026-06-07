#ifndef MM_KMALLOC_H
#define MM_KMALLOC_H

#include <stdint.h>

/**
 * 初始化内核堆分配器。
 *
 * 必须在 page_allocator_init() 之后调用。kmalloc 会在空闲链表耗尽时，通过
 * phys_alloc_pages() 向物理页分配器申请更多页。
 */
void kmalloc_init(void);

/**
 * 分配 size 字节内核内存。
 *
 * 返回的地址只供内核使用。当前内核仍采用恒等映射，所以它可以直接访问
 * phys_alloc_pages() 返回的页；后续引入非恒等 direct map 后，需要在实现中接入
 * phys_to_virt()。
 */
void *kmalloc(uint64_t size);

/**
 * 分配 size 字节内核内存并清零。
 */
void *kzalloc(uint64_t size);

/**
 * 释放 kmalloc/kzalloc 返回的内存块。
 */
void kfree(void *ptr);

#endif

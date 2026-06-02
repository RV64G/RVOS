#ifndef MM_PAGE_ALLOC_H
#define MM_PAGE_ALLOC_H

#include <stdint.h>

/*
 * EFI kernel 路径上的第一版物理页分配器。
 *
 * 它分配的是物理页，返回值当前也是物理地址直通下可直接访问的地址。等内核打开
 * 正式页表、区分物理地址和虚拟地址后，这个接口的返回类型和使用方式需要再收紧。
 */
int page_allocator_init(void);
int page_allocator_ready(void);
uint64_t page_available_pages(void);
void *phys_alloc_pages(uint64_t pages);
void phys_free_pages(void *addr, uint64_t pages);

#endif

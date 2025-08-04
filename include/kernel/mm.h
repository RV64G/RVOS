#ifndef __KERNEL_MM_H__
#define __KERNEL_MM_H__

#include "kernel/types.h"

// 定义系统的物理页大小
#define PAGE_SIZE 4096
#define PAGE_ORDER 12

/* --- 页分配器 (Page Allocator) --- */

// Page descriptor structure (exposed for testing)
struct Page
{
    uint8_t flags;
};

// Page flags
#define PAGE_TAKEN 0x01
#define PAGE_LAST 0x02

// 初始化物理页分配器
void page_init(void);
// 分配指定数量的连续物理页
void *page_alloc(int npages);
// 释放一个通过 page_alloc 分配的内存块
void page_free(void *p);

// Utility functions (exposed for testing)
uintptr_t _align_page(uintptr_t address);
int get_total_pages(void);
int get_allocatable_pages(void);
struct Page *get_page_descriptors(void);

/* --- 块分配器 (Block Allocator / Heap) --- */
// 初始化块分配器 (malloc/free)
void malloc_init(void);
// 分配指定字节大小的内存块
void *malloc(size_t nbytes);
// 释放一个通过 malloc 分配的内存块
void free(void *ptr);
// [调试] 打印空闲链表
void print_free_list(void);
// [调试] 打印指定内存块的内容
void print_block(void *ptr);

#endif /* __KERNEL_MM_H__ */
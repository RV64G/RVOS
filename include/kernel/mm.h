#ifndef __MM_H__
#define __MM_H__

#include "kernel/types.h"

// 定义系统的物理页大小
#define PAGE_SIZE 4096
#define PAGE_ORDER 12

#define PAGE_TAKEN (uint8_t)(1 << 0)
#define PAGE_LAST (uint8_t)(1 << 1)

// Page descriptor structure (exposed for testing)
struct Page
{
	uint8_t flags;
};

void page_init();
void *page_alloc(int npages);
// 释放一个通过 page_alloc 分配的内存块
void page_free(void *p);
int get_total_pages(void);
int get_allocatable_pages(void);
struct Page *get_page_descriptors(void);

// --- Virtual Memory (Sv39) ---

typedef uint64_t pte_t;
typedef uint64_t *pagetable_t; // 512 PTEs

// Page Table Entry Flags
#define PTE_V (1L << 0) // Valid
#define PTE_R (1L << 1) // Read
#define PTE_W (1L << 2) // Write
#define PTE_X (1L << 3) // Execute
#define PTE_U (1L << 4) // User
#define PTE_G (1L << 5) // Global
#define PTE_A (1L << 6) // Accessed
#define PTE_D (1L << 7) // Dirty

// Helper Macros
#define PGROUNDUP(sz) (((sz) + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1))
#define PGROUNDDOWN(a) (((a)) & ~(PAGE_SIZE - 1))

#define PA2PTE(pa) ((((uint64_t)pa) >> 12) << 10)
#define PTE2PA(pte) (((pte) >> 10) << 12)

#define PTE_FLAGS(pte) ((pte) & 0x3FF)

// Extract VPN (Virtual Page Number) from VA
// level 2: bits 30-38
// level 1: bits 21-29
// level 0: bits 12-20
#define PXSHIFT(level) (12 + (level) * 9)
#define PX(level, va) ((((uint64_t)(va)) >> PXSHIFT(level)) & 0x1FF)

// SATP (Supervisor Address Translation and Protection)
// Mode 8 = Sv39
#define SATP_SV39 (8L << 60)
#define MAKE_SATP(pagetable) (SATP_SV39 | (((uint64_t)pagetable) >> 12))

#define MAXVA (1L << (9 + 9 + 9 + 12 - 1))

// --- VM Functions ---
void kvminit(void);
void kvminithart(void);
int kvmmap(pagetable_t pagetable, uint64_t va, uint64_t pa, uint64_t sz, int perm);
pte_t *walk(pagetable_t pagetable, uint64_t va, int alloc);

// --- Heap Allocator ---
void malloc_init(void);
// 分配指定字节大小的内存块
void *malloc(size_t nbytes);
// 释放一个通过 malloc 分配的内存块
void free(void *ptr);
// [调试] 打印空闲链表
void print_free_list(void);
// [调试] 打印指定内存块的内容
void print_block(void *ptr);
#endif
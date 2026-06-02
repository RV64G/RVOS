#include "kernel.h"
#include "kernel/mm.h"
#include "kernel/spinlock.h"

/*
 * 以下全局符号由链接器脚本(os.ld)定义
 * 使用 char[] 确保符号直接解析为地址
 */
extern char _text_start[];
extern char _text_end[];
extern char _rodata_start[];
extern char _rodata_end[];
extern char _data_start[];
extern char _data_end[];
extern char _bss_start[];
extern char _bss_end[];
extern char _memory_start[];
extern char _memory_end[];

/*
 * _alloc_start 指向可供分配内存的起始地址
 * _alloc_end 指向可供分配内存的结束地址
 * _num_pages 保存我们能管理的总物理页数
 */
static uintptr_t _alloc_start = 0;
static uintptr_t _alloc_end = 0;
static uint32_t _num_pages = 0;

// Page allocator spinlock
static struct spinlock page_lock;

// 清除页描述符的标志
static inline void _clear(struct Page *page)
{
	page->flags = 0;
}

// 检查页是否空闲
static inline int _is_free(struct Page *page)
{
	if (page->flags & PAGE_TAKEN)
	{
		return 0;
	}
	else
	{
		return 1;
	}
}

// 设置页描述符的标志
static inline void _set_flag(struct Page *page, uint8_t flags)
{
	page->flags |= flags;
}

// 检查页是否是内存块的最后一页
static inline int _is_last(struct Page *page)
{
	if (page->flags & PAGE_LAST)
	{
		return 1;
	}
	else
	{
		return 0;
	}
}

/*
 * 将地址向上对齐到页边界(4K)
 */
uintptr_t _align_page(uintptr_t address)
{
	uintptr_t order = (1 << PAGE_ORDER) - 1;
	return (address + order) & (~order);
}

void page_init()
{
	// Initialize page allocator spinlock
	spinlock_init(&page_lock, "page_allocator");

	// 从链接器符号获取总内存大小，并计算总页数
	_num_pages = ((uintptr_t)_memory_end - (uintptr_t)_memory_start) / PAGE_SIZE;
	printk("PHYSICAL MEMORY: 0x%lx -> 0x%lx (%ld MB), Total pages: %d\n",
		   (uintptr_t)_memory_start, (uintptr_t)_memory_end,
		   ((uintptr_t)_memory_end - (uintptr_t)_memory_start) / 1024 / 1024,
		   _num_pages);

	// 页描述符数组紧跟在BSS段之后
	struct Page *page_descriptors = (struct Page *)_align_page((uintptr_t)_bss_end);

	// 真正可分配的内存（堆），起始于页描述符数组之后
	_alloc_start = _align_page((uintptr_t)page_descriptors + _num_pages * sizeof(struct Page));
	_alloc_end = (uintptr_t)_memory_end;

	// 计算被内核镜像和页描述符数组自身占用的总页数
	int reserved_pages = (_alloc_start - (uintptr_t)_memory_start) / PAGE_SIZE;

	// 将这些被预留的页标记为 TAKEN
	for (int i = 0; i < reserved_pages; i++)
	{
		_set_flag(&page_descriptors[i], PAGE_TAKEN);
	}

	// --- 打印调试信息 ---
	printk("TEXT:   0x%lx -> 0x%lx\n", (uintptr_t)_text_start, (uintptr_t)_text_end);
	printk("RODATA: 0x%lx -> 0x%lx\n", (uintptr_t)_rodata_start, (uintptr_t)_rodata_end);
	printk("DATA:   0x%lx -> 0x%lx\n", (uintptr_t)_data_start, (uintptr_t)_data_end);
	printk("BSS:    0x%lx -> 0x%lx\n", (uintptr_t)_bss_start, (uintptr_t)_bss_end);
	printk("Page Descriptors Array: 0x%lx -> 0x%lx\n", (uintptr_t)page_descriptors, _alloc_start);
	printk("Kernel, BSS and Page Descriptors reserved space: %d pages (%d KB)\n", reserved_pages, reserved_pages * 4);
	printk("ALLOCATABLE MEMORY (HEAP): 0x%lx -> 0x%lx\n", _alloc_start, _alloc_end);
}

/*
 * 分配一个由连续物理页组成的内存块
 */
void *page_alloc(int npages)
{
	if (npages <= 0 || npages > (int)_num_pages)
	{
		return NULL;
	}

	spinlock_acquire(&page_lock);

	int found = 0;
	struct Page *page_descriptors = (struct Page *)_align_page((uintptr_t)_bss_end);

	for (int i = 0; i <= (int)(_num_pages - npages); i++)
	{
		if (_is_free(&page_descriptors[i]))
		{
			found = 1;
			for (int j = 1; j < npages; j++)
			{
				if (!_is_free(&page_descriptors[i + j]))
				{
					found = 0;
					i += j;
					break;
				}
			}

			if (found)
			{
				for (int k = 0; k < npages; k++)
				{
					_set_flag(&page_descriptors[i + k], PAGE_TAKEN);
				}
				_set_flag(&page_descriptors[i + npages - 1], PAGE_LAST);

				void *result = (void *)((uintptr_t)_memory_start + i * PAGE_SIZE);
				spinlock_release(&page_lock);
				return result;
			}
		}
	}

	spinlock_release(&page_lock);
	return NULL;
}

/*
 * 释放内存块
 */
void page_free(void *p)
{
	if (!p) return;

	spinlock_acquire(&page_lock);

	struct Page *page_descriptors = (struct Page *)_align_page((uintptr_t)_bss_end);
	int page_index = ((uintptr_t)p - (uintptr_t)_memory_start) / PAGE_SIZE;

	if (page_index < 0 || page_index >= (int)_num_pages)
	{
		spinlock_release(&page_lock);
		return;
	}

	struct Page *page = &page_descriptors[page_index];
	while (!_is_free(page))
	{
		if (_is_last(page))
		{
			_clear(page);
			break;
		}
		else
		{
			_clear(page);
			page++;
		}
	}

	spinlock_release(&page_lock);
}

int get_total_pages(void)
{
	return _num_pages;
}

int get_allocatable_pages(void)
{
	int reserved_pages = (_alloc_start - (uintptr_t)_memory_start) / PAGE_SIZE;
	return _num_pages - reserved_pages;
}

struct Page *get_page_descriptors(void)
{
	return (struct Page *)_align_page((uintptr_t)_bss_end);
}
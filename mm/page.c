#include "kernel.h"
#include "kernel/mm.h"
#include "kernel/spinlock.h"

/*
 * 以下全局变量由链接器脚本(os.ld)定义，用于标识内存的关键边界
 */
extern uint32_t TEXT_START;
extern uint32_t TEXT_END;
extern uint32_t DATA_START;
extern uint32_t DATA_END;
extern uint32_t RODATA_START;
extern uint32_t RODATA_END;
extern uint32_t BSS_START;
extern uint32_t BSS_END;
extern uint32_t _memory_start; // RAM的起始地址
extern uint32_t _memory_end;   // RAM的结束地址

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
 * Made public for testing
 */
inline uintptr_t _align_page(uintptr_t address)
{
	uintptr_t order = (1 << PAGE_ORDER) - 1;
	return (address + order) & (~order);
}

void page_init()
{
	// Initialize page allocator spinlock
	spinlock_init(&page_lock, "page_allocator");

	// 从链接器符号获取总内存大小，并计算总页数
	_num_pages = ((uintptr_t)&_memory_end - (uintptr_t)&_memory_start) / PAGE_SIZE;
	printk("PHYSICAL MEMORY: 0x%lx -> 0x%lx (%ld MB), Total pages: %d\n",
		   (uintptr_t)&_memory_start, (uintptr_t)&_memory_end,
		   ((uintptr_t)&_memory_end - (uintptr_t)&_memory_start) / 1024 / 1024,
		   _num_pages);

	// 页描述符数组紧跟在BSS段之后
	struct Page *page_descriptors = (struct Page *)_align_page((uintptr_t)&BSS_END);

	// 真正可分配的内存（堆），起始于页描述符数组之后
	_alloc_start = _align_page((uintptr_t)page_descriptors + _num_pages * sizeof(struct Page));
	_alloc_end = (uintptr_t)&_memory_end;

	// BSS段中的页描述符数组默认就是0，所以所有页状态初始为 FREE (flags=0)
	// 我们只需要将内核和页描述符自身占用的部分标记为 TAKEN

	// 计算被内核镜像和页描述符数组自身占用的总页数
	int reserved_pages = (_alloc_start - (uintptr_t)&_memory_start) / PAGE_SIZE;

	// 将这些被预留的页标记为 TAKEN
	for (int i = 0; i < reserved_pages; i++)
	{
		_set_flag(&page_descriptors[i], PAGE_TAKEN);
	}

	// --- 打印调试信息 ---
	printk("TEXT:   0x%lx -> 0x%lx\n", (uintptr_t)&TEXT_START, (uintptr_t)&TEXT_END);
	printk("RODATA: 0x%lx -> 0x%lx\n", (uintptr_t)&RODATA_START, (uintptr_t)&RODATA_END);
	printk("DATA:   0x%lx -> 0x%lx\n", (uintptr_t)&DATA_START, (uintptr_t)&DATA_END);
	printk("BSS:    0x%lx -> 0x%lx\n", (uintptr_t)&BSS_START, (uintptr_t)&BSS_END);
	printk("Page Descriptors Array: 0x%lx -> 0x%lx\n", (uintptr_t)page_descriptors, _alloc_start);
	printk("Kernel, BSS and Page Descriptors reserved space: %d pages (%d KB)\n", reserved_pages, reserved_pages * 4);
	printk("ALLOCATABLE MEMORY (HEAP): 0x%lx -> 0x%lx\n", _alloc_start, _alloc_end);
}

/*
 * 分配一个由连续物理页组成的内存块
 * - npages: 需要分配的页数
 */
void *page_alloc(int npages)
{
	if (npages <= 0 || npages > _num_pages)
	{
		printk("WARNING: page_alloc called with invalid npages=%d (max=%d)\n", npages, _num_pages);
		return NULL; // 防止无效的页数请求
	}

	spinlock_acquire(&page_lock);

	int found = 0;
	// 页描述符数组位于内核BSS段之后
	struct Page *page_descriptors = (struct Page *)_align_page((uintptr_t)&BSS_END);

	// 遍历整个物理页表来查找连续的空闲页
	// 添加更严格的边界检查
	if (_num_pages < npages)
	{
		return NULL;
	}

	for (int i = 0; i <= (_num_pages - npages); i++)
	{
		// 添加数组边界检查
		if (i >= _num_pages)
		{
			break;
		}

		if (_is_free(&page_descriptors[i]))
		{
			found = 1;
			// 检查后续 (npages - 1) 个页是否也空闲
			for (int j = 1; j < npages; j++)
			{
				// 严格的边界检查
				if ((i + j) >= _num_pages || !_is_free(&page_descriptors[i + j]))
				{
					found = 0;
					i += j; // 优化：跳过已检查过的页
					break;
				}
			}

			if (found)
			{
				// 最后一次边界检查
				if ((i + npages - 1) >= _num_pages)
				{
					return NULL;
				}

				// 找到连续空闲页，将其标记为 TAKEN
				for (int k = 0; k < npages; k++)
				{
					_set_flag(&page_descriptors[i + k], PAGE_TAKEN);
				}
				// 标记内存块的最后一页
				_set_flag(&page_descriptors[i + npages - 1], PAGE_LAST);

				// 返回分配的内存块的实际物理地址
				void *result = (void *)((uintptr_t)&_memory_start + i * PAGE_SIZE);
				spinlock_release(&page_lock);
				return result;
			}
		}
	}

	spinlock_release(&page_lock);
	return NULL; // 内存不足
}

/*
 * 释放内存块
 * - p: 内存块的起始地址
 */
void page_free(void *p)
{
	// 基本的合法性检查
	if (!p)
	{
		printk("WARNING: page_free called with NULL pointer\n");
		return;
	}

	if ((uintptr_t)p < _alloc_start || (uintptr_t)p >= _alloc_end)
	{
		printk("WARNING: page_free called with invalid address 0x%lx (valid range: 0x%lx-0x%lx)\n",
			   (uintptr_t)p, _alloc_start, _alloc_end);
		return;
	}

	spinlock_acquire(&page_lock);

	// 页描述符数组位于内核BSS段之后
	struct Page *page_descriptors = (struct Page *)_align_page((uintptr_t)&BSS_END);

	// 根据物理地址计算它在页描述符数组中的索引
	int page_index = ((uintptr_t)p - (uintptr_t)&_memory_start) / PAGE_SIZE;

	// 添加边界检查
	if (page_index < 0 || page_index >= _num_pages)
	{
		printk("ERROR: page_free: page_index %d out of bounds (0-%d)\n", page_index, _num_pages - 1);
		return;
	}

	struct Page *page = &page_descriptors[page_index];
	struct Page *end_of_descriptors = &page_descriptors[_num_pages];

	// 检查页面是否已分配
	if (_is_free(page))
	{
		printk("WARNING: page_free: trying to free already free page %d\n", page_index);
		return;
	}

	// 循环释放属于同一个内存块的所有页，并增加边界检查
	int release_count = 0;
	while (!_is_free(page) && page < end_of_descriptors)
	{
		release_count++;
		if (release_count > _num_pages)
		{
			printk("ERROR: page_free: infinite loop detected, breaking\n");
			break;
		}

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

/* --- Public utility functions for testing --- */

// Get total pages in the system
int get_total_pages(void)
{
	return _num_pages;
}

// Get allocatable pages (total minus reserved)
int get_allocatable_pages(void)
{
	int reserved_pages = (_alloc_start - (uintptr_t)&_memory_start) / PAGE_SIZE;
	return _num_pages - reserved_pages;
}

// Get page descriptors array pointer
struct Page *get_page_descriptors(void)
{
	return (struct Page *)_align_page((uintptr_t)&BSS_END);
}

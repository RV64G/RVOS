#ifdef KERNEL_SELFTEST

#include "selftest.h"

#include "kmalloc.h"
#include "page_alloc.h"
#include "printk.h"
#include "vm.h"

#define SELFTEST_KMALLOC_BATCH 128ULL
#define KMALLOC_EXHAUST_OBJECT_SIZE (64ULL * 1024ULL)

static int fail(const char *name)
{
    printk("Kernel selftest failed: ");
    printk(name);
    printk("\r\n");
    return 0;
}

static void fill_bytes(void *ptr, uint64_t size, uint8_t value)
{
    uint8_t *bytes = (uint8_t *)ptr;
    for (uint64_t i = 0; i < size; i++)
    {
        bytes[i] = value;
    }
}

static int bytes_are_zero(const void *ptr, uint64_t size)
{
    const uint8_t *bytes = (const uint8_t *)ptr;
    for (uint64_t i = 0; i < size; i++)
    {
        if (bytes[i] != 0)
        {
            return 0;
        }
    }
    return 1;
}

static int test_page_allocator(void)
{
    printk("Selftest page_alloc start\r\n");

    uint64_t before = page_available_pages();
    if (before < 16)
    {
        return fail("page_alloc has too few free pages");
    }

    void *run = phys_alloc_pages(4);
    if (!run)
    {
        return fail("page_alloc 4-page run");
    }
    if (page_available_pages() != before - 4)
    {
        return fail("page_alloc count after run allocation");
    }
    for (uint64_t i = 0; i < 4; i++)
    {
        void *page = (void *)((uintptr_t)run + i * VM_PAGE_SIZE);
        if (phys_page_state(page) != PHYS_PAGE_ALLOCATED)
        {
            return fail("page_alloc allocated state");
        }
    }
    phys_free_pages(run, 4);
    if (page_available_pages() != before)
    {
        return fail("page_alloc count after run free");
    }

    phys_free_pages(run, 4);
    if (page_available_pages() != before)
    {
        return fail("page_alloc double free changed count");
    }

    /*
     * 真正把页分配器压到耗尽：记录每次分到的页，直到 phys_alloc_pages(1)
     * 返回失败。这个测试会覆盖 first-fit 扫描到内存尾部、free_pages 归零、失败
     * 返回路径，以及大批量释放后的计数恢复。
     *
     * 为了不让 kmalloc 额外吃页、干扰计数，测试直接把“下一页”的指针写进刚分到
     * 的页开头，把所有测试页串成单链表。页属于测试代码，写入它们是安全的。
     */
    void *page_list = 0;
    uint64_t allocated = 0;
    for (;;)
    {
        void *page = phys_alloc_pages(1);
        if (!page)
        {
            break;
        }
        if (phys_page_state(page) != PHYS_PAGE_ALLOCATED)
        {
            return fail("page_alloc exhaustion state");
        }
        *(void **)page = page_list;
        page_list = page;
        allocated++;
    }

    if (allocated != before)
    {
        return fail("page_alloc exhaustion count");
    }
    if (page_available_pages() != 0)
    {
        return fail("page_alloc exhaustion free count");
    }
    if (phys_alloc_pages(1) != 0)
    {
        return fail("page_alloc allocation after exhaustion");
    }

    while (page_list)
    {
        void *page = page_list;
        page_list = *(void **)page_list;
        phys_free_pages(page, 1);
    }
    if (page_available_pages() != before)
    {
        return fail("page_alloc count after exhaustion free");
    }

    printk("Selftest page_alloc done\r\n");
    return 1;
}

static int test_vm(void)
{
    printk("Selftest vm start\r\n");

    struct vm_space space;
    if (!vm_space_create(&space))
    {
        return fail("vm_space_create");
    }

    void *phys = phys_alloc_pages(4);
    if (!phys)
    {
        return fail("vm test backing pages");
    }

    uint64_t pa = (uint64_t)(uintptr_t)phys;
    uint64_t va = 0x40000000ULL;
    if (!vm_map_range(&space, va, pa, VM_PAGE_SIZE * 4,
                      VM_MAP_READ | VM_MAP_WRITE))
    {
        return fail("vm_map_range basic");
    }

    struct vm_mapping mapping;
    if (!vm_query(&space, va + 123, &mapping))
    {
        return fail("vm_query mapped address");
    }
    if (mapping.pa != pa + 123 || mapping.leaf_size != VM_PAGE_SIZE ||
        mapping.flags != (VM_MAP_READ | VM_MAP_WRITE))
    {
        return fail("vm_query mapped details");
    }

    if (!vm_unmap_range(&space, va, VM_PAGE_SIZE * 4))
    {
        return fail("vm_unmap_range basic");
    }
    if (vm_query(&space, va, &mapping))
    {
        return fail("vm_query after unmap");
    }

    uint64_t rollback_va = 0x50000000ULL;
    uint64_t conflict_va = rollback_va + VM_PAGE_SIZE;
    if (!vm_map_range(&space, conflict_va, pa + VM_PAGE_SIZE * 3,
                      VM_PAGE_SIZE, VM_MAP_READ))
    {
        return fail("vm rollback setup");
    }

    uint64_t mapped_pages_before = space.mapped_pages;
    if (vm_map_range(&space, rollback_va, pa, VM_PAGE_SIZE * 3, VM_MAP_READ))
    {
        return fail("vm rollback conflict was accepted");
    }
    if (vm_query(&space, rollback_va, &mapping))
    {
        return fail("vm rollback left prefix mapping");
    }
    if (!vm_query(&space, conflict_va, &mapping))
    {
        return fail("vm rollback removed old mapping");
    }
    if (space.mapped_pages != mapped_pages_before)
    {
        return fail("vm rollback mapped_pages");
    }
    if (!vm_unmap_range(&space, conflict_va, VM_PAGE_SIZE))
    {
        return fail("vm rollback cleanup");
    }

    phys_free_pages(phys, 4);

    printk("Selftest vm done\r\n");
    return 1;
}

static int test_kmalloc(void)
{
    printk("Selftest kmalloc start\r\n");

    void *zeroed = kzalloc(257);
    if (!zeroed)
    {
        return fail("kzalloc allocation");
    }
    if (!bytes_are_zero(zeroed, 257))
    {
        return fail("kzalloc zero fill");
    }
    fill_bytes(zeroed, 257, 0xa5);
    kfree(zeroed);

    void *big = kmalloc(VM_PAGE_SIZE + 512);
    if (!big)
    {
        return fail("kmalloc big allocation");
    }
    fill_bytes(big, VM_PAGE_SIZE + 512, 0x5a);
    kfree(big);

    void *ptrs[SELFTEST_KMALLOC_BATCH];
    for (uint64_t i = 0; i < SELFTEST_KMALLOC_BATCH; i++)
    {
        uint64_t size = 1 + ((i * 37) % 1024);
        ptrs[i] = kmalloc(size);
        if (!ptrs[i])
        {
            return fail("kmalloc batch allocation");
        }
        fill_bytes(ptrs[i], size, (uint8_t)i);
    }

    for (uint64_t i = 0; i < SELFTEST_KMALLOC_BATCH; i += 2)
    {
        kfree(ptrs[i]);
        ptrs[i] = 0;
    }
    for (uint64_t i = 0; i < SELFTEST_KMALLOC_BATCH; i += 2)
    {
        ptrs[i] = kmalloc(64 + i);
        if (!ptrs[i])
        {
            return fail("kmalloc reuse allocation");
        }
    }
    for (uint64_t i = SELFTEST_KMALLOC_BATCH; i > 0; i--)
    {
        kfree(ptrs[i - 1]);
    }

    printk("Selftest kmalloc done\r\n");
    return 1;
}

static int test_kmalloc_exhaustion(void)
{
    printk("Selftest kmalloc exhaustion start\r\n");

    /*
     * 当前 kmalloc 堆只增长、不收缩：grow_heap() 从 page allocator 拿到的页即使
     * 后续通过 kfree() 回到 kmalloc 空闲链表，也不会再归还给 phys_free_pages()。
     *
     * 因此这个测试必须放在 selftest 最后一项。它会反复申请较大的对象，直到
     * kmalloc 无法再从 page allocator 扩张并返回 0；随后把对象还给 kmalloc，
     * 验证 kfree 链表路径不会崩，但不要求 page_available_pages() 恢复到测试前。
     *
     * 对象大小故意选 64KB，而不是 4KB。这样仍然会把 kmalloc/page allocator 压到
     * 耗尽，但循环次数少得多，更适合放进 CI 的 make test。
     */
    void *object_list = 0;
    uint64_t allocated = 0;

    for (;;)
    {
        void *object = kmalloc(KMALLOC_EXHAUST_OBJECT_SIZE);
        if (!object)
        {
            break;
        }

        *(void **)object = object_list;
        object_list = object;
        allocated++;
    }

    if (allocated == 0)
    {
        return fail("kmalloc exhaustion allocated no objects");
    }
    if (kmalloc(KMALLOC_EXHAUST_OBJECT_SIZE) != 0)
    {
        return fail("kmalloc allocation after exhaustion");
    }

    while (object_list)
    {
        void *object = object_list;
        object_list = *(void **)object_list;
        kfree(object);
    }

    printk("Selftest kmalloc exhaustion done\r\n");
    return 1;
}

int kernel_selftest_run(void)
{
    printk("Kernel selftest start\r\n");

    if (!test_page_allocator())
    {
        return 0;
    }
    if (!test_vm())
    {
        return 0;
    }
    if (!test_kmalloc())
    {
        return 0;
    }
    if (!test_kmalloc_exhaustion())
    {
        return 0;
    }

    return 1;
}

#endif

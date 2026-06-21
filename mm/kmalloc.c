#include "kmalloc.h"

#include "align.h"
#include "page_alloc.h"
#include "string.h"

#define KMALLOC_PAGE_SIZE 4096ULL

/*
 * 第一版内核堆采用 K&R 风格的循环空闲链表。
 *
 * 每个块前面都有一个 Header。Header::units 的单位不是字节，而是 Header 数量：
 *
 *   [Header][用户数据...]
 *
 * 如果 sizeof(Header) == 16，units == 4 表示这个块总长 64 字节，其中第一个
 * Header 用来记录元数据，剩下 48 字节才是可返回给调用者的用户区。使用 Header
 * 作为单位后，block + block->units 正好能指向块尾，方便判断相邻块能否合并。
 *
 * Header 里有指针和 uint64_t，RV64 下天然按 8 字节对齐；当前内核对象分配先以这个
 * 对齐粒度为准。若后续需要更严格对齐，再单独扩展 kmalloc_aligned()。
 *
 * 空闲链表按地址顺序组织，kfree() 插回链表时尝试和前后相邻空闲块合并。这样实现
 * 足够小，能支撑 timer、PCB、文件系统节点等小对象；等多核和调度真正接入后，再给
 * 临界区加自旋锁。
 */
typedef struct heap_header
{
    struct heap_header *next;
    uint64_t units;
} Header;

static Header base;
static Header *free_list;

void kmalloc_init(void)
{
    base.next = &base;
    base.units = 0;
    free_list = &base;
}

static int add_block_to_free_list(Header *block)
{
    if (!free_list || !block)
    {
        return 0;
    }

    Header *current = free_list;

    /*
     * 普通插入位置满足 current < block < current->next。
     *
     * 这里的 for 条件看起来绕，是因为 free_list 是按地址排序的循环链表。对“继续
     * 条件”取反后，真正的停止条件就是找到了 current 和 current->next 之间的位置。
     *
     * 如果 current >= current->next，说明这里是循环链表的地址边界：current 是当前
     * 最高地址块，current->next 是当前最低地址块。此时 block 如果比 current 还高，
     * 或比 current->next 还低，都应该插在这个边界处。
     */
    for (; !(block > current && block < current->next); current = current->next)
    {
        if (current >= current->next &&
            (block > current || block < current->next))
        {
            break;
        }
    }

    if (block + block->units == current->next)
    {
        block->units += current->next->units;
        block->next = current->next->next;
    }
    else
    {
        block->next = current->next;
    }

    if (current + current->units == block)
    {
        current->units += block->units;
        current->next = block->next;
    }
    else
    {
        current->next = block;
    }

    free_list = current;
    return 1;
}

static Header *grow_heap(uint64_t units)
{
    /*
     * K&R malloc 传统上把这一步叫 morecore。这里的 core 不是 CPU core，而是早期
     * 对主存的叫法。为了避免混淆，这里改叫 grow_heap：从页分配器拿页，并把新页
     * 作为空闲块接进 kmalloc 堆。
     */
    if (units > UINT64_MAX / sizeof(Header))
    {
        return 0;
    }
    uint64_t bytes = units * sizeof(Header);

    if (bytes > UINT64_MAX - (KMALLOC_PAGE_SIZE - 1))
    {
        return 0;
    }

    uint64_t pages = align_up(bytes, KMALLOC_PAGE_SIZE) / KMALLOC_PAGE_SIZE;

    Header *block = (Header *)phys_alloc_pages(pages);
    if (!block)
    {
        return 0;
    }

    /*
     * 从页分配器拿到的新页整体变成一个空闲堆块。之后即使 kfree() 合并出完整空页，
     * 当前第一版也不会把页还给 phys_free_pages()，而是留给 kmalloc 后续复用。
     */
    block->units = (pages * KMALLOC_PAGE_SIZE) / sizeof(Header);
    if (!add_block_to_free_list(block))
    {
        return 0;
    }
    return free_list;
}

void *kmalloc(uint64_t size)
{
    if (!free_list || size == 0)
    {
        return 0;
    }

    if (size > UINT64_MAX - sizeof(Header))
    {
        return 0;
    }

    uint64_t units = (size + sizeof(Header) - 1) / sizeof(Header) + 1;
    Header *previous = free_list;

    for (;;)
    {
        Header *current = previous->next;

        if (current->units >= units)
        {
            if (current->units == units)
            {
                previous->next = current->next; // current被拆下
            }
            else
            {
                /*
                 * 从空闲块尾部切出要分配的部分。这样原空闲块仍留在链表原位置，
                 * 只需要缩小 units，不需要重新接链表指针。
                 */
                current->units -= units;
                current += current->units;
                current->units = units;
            }

            free_list = previous;
            return (void *)(current + 1);
        }

        if (current == free_list)
        {
            current = grow_heap(units);
            if (!current)
            {
                return 0;
            }
            previous = free_list;
        }
        else
        {
            previous = current;
        }
    }
}

void *kzalloc(uint64_t size)
{
    void *ptr = kmalloc(size);
    if (ptr)
    {
        memzero(ptr, size);
    }
    return ptr;
}

void kfree(void *ptr)
{
    if (!ptr || !free_list)
    {
        return;
    }

    Header *block = (Header *)ptr - 1;
    add_block_to_free_list(block);
}

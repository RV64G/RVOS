// 一个基于K&R C书中设计的简单内存分配器。
// 它从底层的页分配器获取内存。

#include "kernel.h"
#include "kernel/mm.h"
#include "kernel/spinlock.h"

// 内存块头部结构。每个内存块（无论是已分配还是空闲）都由此头部开始。
// 使用union来确保头部按最严格的对齐要求（long）对齐。
typedef union header
{
    struct
    {
        union header *next; // 指向空闲链表中的下一个块
        size_t size;        // 当前块的大小，单位是sizeof(Header)
    } s;
    long align; // 强制对齐
} Header;

static Header base;          // 用于构建空闲链表的起始空块
static Header *freep = NULL; // 空闲链表的起始指针

// Malloc subsystem spinlock
static struct spinlock malloc_lock;

// free函数的前向声明
void free(void *ptr);

// `morecore`: 向页分配器申请更多内存。
// 当`malloc`耗尽内存时会调用此函数。
static Header *morecore(size_t nunits)
{
    char *cp;
    Header *up;
    int npages;

    // 我们以页为单位向页分配内存。
    // 计算需要多少页来满足`nunits`的需求。
    // 至少申请一整个页。
    npages = (nunits * sizeof(Header) + PAGE_SIZE - 1) / PAGE_SIZE;

    cp = page_alloc(npages);
    if (cp == NULL)
    {
        // 页分配器内存不足。
        return NULL;
    }

    up = (Header *)cp;
    // 新块的大小是页数转换成Header为单位的数量。
    up->s.size = (npages * PAGE_SIZE) / sizeof(Header);

    // 将新申请的内存加入到空闲链表中。
    // `free`函数会处理与相邻空闲块的合并。
    // 我们传递`up + 1`是因为`free`期望接收一个指向用户数据区的指针，
    // 该区域紧跟在头部之后。
    free((void *)(up + 1));

    // `free`会更新`freep`，我们将其返回给调用者。
    return freep;
}

// `malloc_init`: 初始化内存分配器。
// 必须在内核启动时调用一次。
void malloc_init()
{
    // Initialize malloc subsystem spinlock
    spinlock_init(&malloc_lock, "malloc_allocator");

    base.s.next = &base; // 循环链表，初始指向自身
    base.s.size = 0;     // 大小为0
    freep = &base;       // 空闲链表初始为空
}

// `malloc`: 分配一个至少为`nbytes`大小的内存块。
void *malloc(size_t nbytes)
{
    Header *p, *prevp;
    size_t nunits;

    if (nbytes == 0)
    {
        return NULL;
    }

    spinlock_acquire(&malloc_lock);

    // 计算需要多少个Header大小的单元。
    // 我们为头部本身加1，并向上取整，以确保用户数据区足够大。
    nunits = (nbytes + sizeof(Header) - 1) / sizeof(Header) + 1;

    prevp = freep;
    // 遍历循环空闲链表，查找一个足够大的块。
    for (p = prevp->s.next;; prevp = p, p = p->s.next)
    {
        if (p->s.size >= nunits)
        { // 找到了一个足够大的块
            if (p->s.size == nunits)
            {
                // 大小正好合适，直接从链表中移除。
                prevp->s.next = p->s.next;
            }
            else
            {
                // 块比需求大，从尾部分配。
                // 这样可以避免修改空闲链表中的指针。
                p->s.size -= nunits;
                p += p->s.size;
                p->s.size = nunits;
            }
            // 将下一次malloc的搜索起点设置为我们找到的块的前一个块。
            // 这有助于在堆中分散分配。
            freep = prevp;
            // 返回指向用户数据区的指针，就在头部之后。
            void *result = (void *)(p + 1);
            spinlock_release(&malloc_lock);
            return result;
        }

        // 如果遍历完整个空闲链表都没有找到合适的块。
        if (p == freep)
        {
            // 向系统申请更多内存。
            if ((p = morecore(nunits)) == NULL)
            {
                // 内存耗尽。
                spinlock_release(&malloc_lock);
                return NULL;
            }
        }
    }
}

// `free`: 释放一个内存块，将其归还到空闲链表。
void free(void *ptr)
{
    Header *bp, *p;

    if (ptr == NULL)
    {
        return;
    }

    spinlock_acquire(&malloc_lock);

    // 获取指向块头部的指针。
    bp = (Header *)ptr - 1;

    // 在空闲链表中找到合适的位置插入被释放的块。
    // 链表按地址排序，以便进行合并。
    for (p = freep; !(bp > p && bp < p->s.next); p = p->s.next)
    {
        // `p >= p->s.next`表示我们到达了循环链表的“末端”
        // (即p指向地址最高的块)。
        if (p >= p->s.next && (bp > p || bp < p->s.next))
        {
            break;
        }
    }

    // 尝试与下一个块（内存地址更高的块）合并。
    if (bp + bp->s.size == p->s.next)
    {
        bp->s.size += p->s.next->s.size;
        bp->s.next = p->s.next->s.next;
    }
    else
    {
        bp->s.next = p->s.next;
    }

    // 尝试与前一个块（内存地址更低的块）合并。
    if (p + p->s.size == bp)
    {
        p->s.size += bp->s.size;
        p->s.next = bp->s.next;
    }
    else
    {
        p->s.next = bp;
    }

    // 将空闲链表的指针重置到我们当前的位置。
    freep = p;

    spinlock_release(&malloc_lock);
}

// --- 调试函数 ---

// 打印当前所有空闲块的信息
void print_free_list()
{
    Header *p;
    printk("-- start to print free list --\n");
    // 从空闲链表的起点开始遍历，直到绕回起点
    for (p = freep->s.next; p != &base; p = p->s.next)
    {
        printk("\tfree block: %p, size: %d units (%d bytes)\n",
               (void *)p, p->s.size, p->s.size * sizeof(Header));
    }
    printk("-- end to print free list --\n");
}

// 打印指定内存块的内容（以整数形式）
void print_block(void *ptr)
{
    if (ptr == NULL)
    {
        printk("print_block: trying to print a NULL pointer.\n");
        return;
    }
    Header *bp = (Header *)ptr - 1;
    // 计算用户数据区的实际字节大小
    size_t user_size_bytes = (bp->s.size - 1) * sizeof(Header);
    void *block_end = (char *)ptr + user_size_bytes;
    int byte_count = 1;
    int *int_block_ptr = (int *)ptr;

    printk("--- start content of block at %p (size: %d bytes) ---\n", ptr, user_size_bytes);
    for (; (void *)int_block_ptr < block_end; int_block_ptr++, byte_count++)
    {
        printk("%d", (*int_block_ptr));
        if (byte_count % 4 == 0)
            printk(" ");
        if (byte_count % 32 == 0)
            printk("\n");
    }
    printk("\n--- end content of block ---\n\n");
}

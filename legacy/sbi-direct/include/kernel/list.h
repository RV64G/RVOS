#ifndef __KERNEL_LIST_H__
#define __KERNEL_LIST_H__

#include "kernel/types.h"

/*
 * 一个极简但功能完备的双向链表实现，其设计思想源自于 Linux 内核。
 * 它不存储数据本身，只负责将包含它的结构体链接起来。
 */

// 1. 链表节点 (list_head)
// 这是链表的基本构造单元。它不包含数据，应该被“嵌入”到你自己的数据结构中。
struct list_head {
    struct list_head *next, *prev; // 指向前一个和后一个节点
};

// 2. 静态初始化链表头 (LIST_HEAD)
// 定义并初始化一个链表头。这会创建一个独立的、空的、指向自身的循环链表。
// `name` 就是你给这个链表头起的名字。
#define LIST_HEAD_INIT(name) { &(name), &(name) }
#define LIST_HEAD(name) \
    struct list_head name = LIST_HEAD_INIT(name)

/**
 * @brief 动态初始化一个链表节点（或头）
 * @param list 指向需要被初始化的 list_head 结构体的指针。
 *
 * 当你无法使用 LIST_HEAD 宏（比如 list_head 是动态分配的）时使用此函数。
 * 它同样会将一个节点初始化为指向自身的空链表。
 */
static inline void INIT_LIST_HEAD(struct list_head *list)
{
    list->next = list;
    list->prev = list;
}

/**
 * @brief 内部辅助函数：在两个已知连续的节点之间插入一个新节点。
 * @param new       要插入的新节点
 * @param prev      前驱节点
 * @param next      后继节点
 *
 * 这是所有插入操作的核心，但不应该被直接调用。
 */
static inline void __list_add(struct list_head *new_entry,
                              struct list_head *prev,
                              struct list_head *next)
{
    next->prev = new_entry;
    new_entry->next = next;
    new_entry->prev = prev;
    prev->next = new_entry;
}

/**
 * @brief 在指定节点之后插入一个新节点 (头插法)
 * @param new  要插入的新节点
 * @param head 链表中的一个现有节点（通常是头节点）
 *
 * 这个函数常用于实现栈 (LIFO, 后进先出)。
 */
static inline void list_add(struct list_head *new_entry, struct list_head *head)
{
    __list_add(new_entry, head, head->next);
}

/**
 * @brief 在指定节点之前插入一个新节点 (尾插法)
 * @param new  要插入的新节点
 * @param head 链表中的一个现有节点（通常是头节点）
 *
 * 这个函数常用于实现队列 (FIFO, 先进先出)。
 */
static inline void list_add_tail(struct list_head *new_entry, struct list_head *head)
{
    __list_add(new_entry, head->prev, head);
}

/**
 * @brief 内部辅助函数：通过修改指针来“断开”一个节点。
 * @param prev  要断开节点的前驱节点
 * @param next  要断开节点的后继节点
 *
 * 这个函数只负责修改指针，被断开的节点本身不会被修改。
 */
static inline void __list_del(struct list_head * prev, struct list_head * next)
{
	next->prev = prev;
	prev->next = next;
}

/**
 * @brief 从链表中删除一个节点。
 * @param entry 要从链表中删除的节点。
 *
 * 注意：这个函数只负责将 `entry` 从它所在的链表中“解绑”，
 * `entry` 节点的 next 和 prev 指针仍然保留着旧的地址，处于一种无效状态。
 * 这个函数并不会释放 `entry` 所属的那个大结构体的内存。
 */
static inline void list_del(struct list_head *entry)
{
	__list_del(entry->prev, entry->next);
    // 我们可以通过把指针设置为无效值（比如 LIST_POISON1）来防止后续的误用，
    // 但为了简单，我们暂时不这么做。
	// entry->next = NULL;
	// entry->prev = NULL;
}

/**
 * @brief 检查一个链表是否为空。
 * @param head 要检查的链表的头节点。
 * @return 如果链表为空则返回 true (非零)，否则返回 false (零)。
 */
static inline int list_empty(const struct list_head *head)
{
	return head->next == head;
}

/**
 * @brief 从一个 list_head 成员指针，反向计算出其宿主结构体的指针。
 * @param ptr     指向 list_head 成员的指针。
 * @param type    宿主结构体的类型 (例如: struct task_struct)。
 * @param member  list_head 成员在宿主结构体中的名字。
 *
 * 这是这套链表实现中最核心的魔法。它让我们能从一个通用的链表节点，
 * 找到包含它的、具体的、带有数据的那个大结构体。
 */
#define list_entry(ptr, type, member) \
    container_of(ptr, type, member)

/**
 * @brief 遍历一个链表（操作的是 list_head 节点本身）。
 * @param pos   一个 `struct list_head *` 类型的指针，用作循环游标。
 * @param head  要遍历的链表的头节点。
 */
#define list_for_each(pos, head) \
    for (pos = (head)->next; pos != (head); pos = pos->next)

/**
 * @brief 遍历一个链表（操作的是包含 list_head 的宿主结构体）。
 * @param pos    一个指向你的宿主结构体类型的指针，用作循环游标。
 * @param head   要遍历的链表的头节点。
 * @param member list_head 成员在宿主结构体中的名字。
 *
 * 这是最常用、最方便的遍历宏。
 */
#define list_for_each_entry(pos, head, member) \
    for (pos = list_entry((head)->next, typeof(*pos), member); \
         &pos->member != (head); \
         pos = list_entry(pos->member.next, typeof(*pos), member))

#endif /* __KERNEL_LIST_H__ */
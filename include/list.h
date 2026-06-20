#ifndef KERNEL_LIST_H
#define KERNEL_LIST_H

#include <stddef.h>

/*
 * 嵌入式双向链表。链表节点不单独分配内存，而是作为成员嵌进宿主结构体。
 *
 * 例如 task 的 run queue 不保存 struct task* 数组，而是在 struct task 内放一个
 * struct list_head run_node。调度器拿到 run_node 后，再用 list_entry() 找回
 * task。
 */
struct list_head
{
    struct list_head *next;
    struct list_head *prev;
};

#define LIST_HEAD_INIT(name) {&(name), &(name)}
#define LIST_HEAD(name) struct list_head name = LIST_HEAD_INIT(name)

static inline void list_init(struct list_head *list)
{
    list->next = list;
    list->prev = list;
}

static inline int list_empty(const struct list_head *head)
{
    return head->next == head;
}

static inline void list_add_between(struct list_head *entry,
                                    struct list_head *prev,
                                    struct list_head *next)
{
    next->prev = entry;
    entry->next = next;
    entry->prev = prev;
    prev->next = entry;
}

static inline void list_add(struct list_head *entry, struct list_head *head)
{
    list_add_between(entry, head, head->next);
}

static inline void list_add_tail(struct list_head *entry,
                                 struct list_head *head)
{
    list_add_between(entry, head->prev, head);
}

static inline void list_del(struct list_head *entry)
{
    entry->next->prev = entry->prev;
    entry->prev->next = entry->next;
    list_init(entry);
}

#define container_of(ptr, type, member)                                        \
    ((type *)((char *)(ptr) - offsetof(type, member)))

#define list_entry(ptr, type, member) container_of(ptr, type, member)

#define list_first_entry(head, type, member)                                   \
    list_entry((head)->next, type, member)

#define list_for_each(pos, head)                                               \
    for (pos = (head)->next; pos != (head); pos = pos->next)

#define list_for_each_entry(pos, head, member)                                 \
    for (pos = list_entry((head)->next, __typeof__(*pos), member);             \
         &pos->member != (head);                                               \
         pos = list_entry(pos->member.next, __typeof__(*pos), member))
/*
 * list_head 通常内嵌在别的结构体里使用。链表本身只串联 list_head 节点；
 * 需要访问数据时，用 list_entry()/list_first_entry() 从 list_head
 * 地址反推出宿主结构体。
 *
 * type 是宿主结构体类型，例如 struct task；member 是宿主结构体中 list_head
 * 成员的名字， 例如 struct task 里的 run_node。
 */
#endif

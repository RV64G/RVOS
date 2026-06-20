#ifndef KERNEL_SPINLOCK_H
#define KERNEL_SPINLOCK_H

#include <stdint.h>

struct spinlock
{
    volatile uint32_t locked;
    uint64_t saved_sstatus;
    const char *name;
};

/**
 * 初始化自旋锁。
 */
void spinlock_init(struct spinlock *lock, const char *name);

/**
 * 获取自旋锁。
 *
 * 获取前会关闭当前 hart 的 S-mode 中断，避免同一 hart 在持锁期间被中断打断后，
 * 中断处理路径再次申请同一把锁造成死锁。
 */
void spinlock_acquire(struct spinlock *lock);

/**
 * 释放自旋锁，并恢复获取锁前的 S-mode 中断状态。
 */
void spinlock_release(struct spinlock *lock);

/**
 * 返回锁当前是否处于持有状态。
 */
int spinlock_is_locked(const struct spinlock *lock);

#endif

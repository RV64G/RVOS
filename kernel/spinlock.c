#include "spinlock.h"

#include <stdint.h>

#define SSTATUS_SIE (1ULL << 1)

static uint64_t irq_save(void)
{
    uint64_t old_sstatus;

    __asm__ volatile(
        "csrrc %0, sstatus, %1\n"
        : "=r"(old_sstatus)
        : "r"(SSTATUS_SIE)
        : "memory"
    );
    return old_sstatus;
}

static void irq_restore(uint64_t old_sstatus)
{
    if ((old_sstatus & SSTATUS_SIE) != 0)
    {
        __asm__ volatile("csrs sstatus, %0" : : "r"(SSTATUS_SIE) : "memory");
    }
    else
    {
        __asm__ volatile("csrc sstatus, %0" : : "r"(SSTATUS_SIE) : "memory");
    }
}

static uint32_t atomic_swap_acquire(volatile uint32_t *addr, uint32_t value)
{
    uint32_t old;

    /*
     * AMO 的 aq 位提供 acquire 语义：成功拿到锁之后，后续内存访问不能被重排到
     * 抢锁之前。RISC-V A 扩展保证这个读-改-写动作对其它 hart 原子可见。
     */
    __asm__ volatile(
        "amoswap.w.aq %0, %2, (%1)"
        : "=r"(old)
        : "r"(addr), "r"(value)
        : "memory"
    );
    return old;
}

static void atomic_store_release(volatile uint32_t *addr, uint32_t value)
{
    /*
     * AMO 的 rl 位提供 release 语义：释放锁之前的内存访问不能被重排到解锁之后。
     * 这里用 amoswap 写 0，而不是普通 store，保持和 acquire 侧同一类原子访问。
     */
    __asm__ volatile(
        "amoswap.w.rl x0, %1, (%0)"
        :
        : "r"(addr), "r"(value)
        : "memory"
    );
}

static void spin_relax(void)
{
    __asm__ volatile("nop" ::: "memory");
}

void spinlock_init(struct spinlock *lock, const char *name)
{
    lock->locked = 0;
    lock->saved_sstatus = 0;
    lock->name = name;
}

void spinlock_acquire(struct spinlock *lock)
{
    uint64_t old_sstatus = irq_save();

    while (atomic_swap_acquire(&lock->locked, 1) != 0)
    {
        while (lock->locked)
        {
            spin_relax();
        }
    }

    lock->saved_sstatus = old_sstatus;
}

void spinlock_release(struct spinlock *lock)
{
    uint64_t old_sstatus = lock->saved_sstatus;

    lock->saved_sstatus = 0;
    atomic_store_release(&lock->locked, 0);
    irq_restore(old_sstatus);
}

int spinlock_is_locked(const struct spinlock *lock)
{
    return lock->locked != 0;
}

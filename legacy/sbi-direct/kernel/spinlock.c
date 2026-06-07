#include <kernel/spinlock.h>
#include <arch/riscv.h>
#include <kernel/printk.h>

// Uncomment the following line to enable spinlock debugging
// #define DEBUG_SPINLOCK

void spinlock_init(struct spinlock *lock, char *name)
{
	lock->locked = 0;
#ifdef DEBUG_SPINLOCK
	lock->name = name;
	lock->hartid = -1;
#endif
}

// Acquire the lock.
// Loops (spins) until the lock is acquired.
void spinlock_acquire(struct spinlock *lock)
{
	lock->old_sstatus = push_off(); // disable interrupts to avoid deadlock.
	
#ifdef DEBUG_SPINLOCK
	if (lock->locked && lock->hartid == r_hartid()) {
		panic("spinlock %s: re-acquisition", lock->name);
	}
#endif

	// On RISC-V, amoswap.w.aq reads the old value from lock->locked and writes 1 into it.
	// The ".aq" (acquire) memory ordering semantics prevent subsequent memory accesses
	// from being reordered before this one.
	// This is a Test-and-Test-and-Set (TTAS) lock.
	while (__sync_lock_test_and_set(&lock->locked, 1) != 0) {
		while(lock->locked) {
			spin_loop_hint();
		}
	}

#ifdef DEBUG_SPINLOCK
	// Record info about lock acquisition for debugging.
	lock->hartid = r_hartid();
#endif
}

// Release the lock.
void spinlock_release(struct spinlock *lock)
{
#ifdef DEBUG_SPINLOCK
	if (!lock->locked || lock->hartid != r_hartid()) {
		panic("spinlock %s: release by non-owner", lock->name);
	}
	lock->hartid = -1;
#endif

	// On RISC-V, amoswap.w.rl writes 0 to lock->locked and returns the old value.
	// The ".rl" (release) memory ordering semantics prevent preceding memory accesses
	// from being reordered after this one.
	__sync_lock_release(&lock->locked);

	pop_off(lock->old_sstatus); // re-enable interrupts.
}

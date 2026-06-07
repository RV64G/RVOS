#ifndef __SPINLOCK_H__
#define __SPINLOCK_H__

#include "kernel/types.h"

// Mutual exclusion lock.
struct spinlock {
	volatile int locked; // Is the lock held?

	// For debugging:
	char *name;      // Name of lock.
	reg_t hartid;    // The hart holding the lock.
	reg_t old_sstatus; // Old sstatus register value before acquiring the lock.
};

void spinlock_init(struct spinlock *lock, char *name);
void spinlock_acquire(struct spinlock *lock);
void spinlock_release(struct spinlock *lock);

#endif // __SPINLOCK_H__

#ifndef __KERNEL_H__
#define __KERNEL_H__

#include "kernel/types.h"
#include "arch/riscv.h"
#include "arch/platform.h"

#include "kernel/printk.h"
#include "kernel/mm.h"
#include "kernel/sched.h"
#include "kernel/timer.h"
#include "kernel/spinlock.h"
#include "kernel/irq.h"
#include "syscalls.h"
#include "kernel/boot_info.h"

#include "stddef.h"
#include "stdarg.h"

// kernel/main.c
void start_kernel(void);

#endif /* __KERNEL_H__ */

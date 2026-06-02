#ifndef __KERNEL_PRINTK_H__
#define __KERNEL_PRINTK_H__

#include "stdarg.h"

/* Kernel console printing functions */
int vprintk(const char *fmt, va_list args);
int printk(const char *fmt, ...);
void panic(const char *s);

#endif /* __KERNEL_PRINTK_H__ */

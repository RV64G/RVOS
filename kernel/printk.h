#ifndef KERNEL_PRINTK_H
#define KERNEL_PRINTK_H

#include <stdint.h>

/*
 * 内核正式日志入口。
 *
 * DTB 解析完成后，printk_init() 会优先使用 platform_info 中的 UART；UART 不可用时
 * 保持 SBI console fallback。调用侧只依赖 printk 语义，不需要直接知道当前后端。
 */
void printk_init(void);
void printk(const char *s);
void printk_hex64(uint64_t value);
void printk_u64(uint64_t value);
void printk_field(const char *name, uint64_t value);
void printk_dec_field(const char *name, uint64_t value);

#endif

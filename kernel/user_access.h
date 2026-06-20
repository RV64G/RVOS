#ifndef KERNEL_USER_ACCESS_H
#define KERNEL_USER_ACCESS_H

#include <stdint.h>

/**
 * 从用户地址复制到内核缓冲区。
 *
 * 当前只负责按 RISC-V 规则临时打开 sstatus.SUM，让 S-mode 可以访问带 U 位的页。
 * 它还不能从 page fault 中恢复，也没有完整用户地址空间边界检查；这些要等独立
 * 用户 vm_space 和 fault 修复路径出现后再补。
 */
int copy_from_user(void *kernel_dst, const void *user_src, uint64_t size);

/**
 * 从内核缓冲区复制到用户地址。
 *
 * 约束同 copy_from_user()：当前是早期 syscall 基础设施，不是最终安全边界。
 */
int copy_to_user(void *user_dst, const void *kernel_src, uint64_t size);

#endif

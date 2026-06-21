#ifndef KERNEL_SYSCALL_H
#define KERNEL_SYSCALL_H

#include "syscall_numbers.h"
#include "trap.h"

/**
 * 处理来自 U-mode 的 ecall。
 *
 * syscall number 按 Linux RISC-V ABI 从 a7 读取，最多 6 个参数来自 a0..a5，
 * 返回值写回 a0。
 */
void syscall_handle(struct trap_frame *frame);

#endif

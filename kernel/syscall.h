#ifndef KERNEL_SYSCALL_H
#define KERNEL_SYSCALL_H

#include "trap.h"

enum syscall_number
{
    SYS_READ = 63,
    SYS_WRITE = 64,
    SYS_EXIT = 93,
    SYS_SCHED_YIELD = 124,

    /*
     * 临时 demo syscall：a0 直接传毫秒数。后续如果要贴近 Linux ABI，应改成
     * nanosleep/clock_nanosleep 的 timespec 参数。
     */
    SYS_SLEEP_MS = 1000,
};

/**
 * 处理来自 U-mode 的 ecall。
 *
 * syscall number 按 Linux RISC-V ABI 从 a7 读取，最多 6 个参数来自 a0..a5，
 * 返回值写回 a0。
 */
void syscall_handle(struct trap_frame *frame);

#endif

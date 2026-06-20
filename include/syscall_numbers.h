#ifndef RVOS_SYSCALL_NUMBERS_H
#define RVOS_SYSCALL_NUMBERS_H

#define SYS_READ        63
#define SYS_WRITE       64
#define SYS_EXIT        93
#define SYS_SCHED_YIELD 124

/*
 * 临时 demo syscall：a0 直接传毫秒数。后续如果要贴近 Linux ABI，应改成
 * nanosleep/clock_nanosleep 的 timespec 参数。
 */
#define SYS_SLEEP_MS 1000

#endif

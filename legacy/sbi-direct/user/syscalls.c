#include "syscalls.h"

// 统一的系统调用原始接口
static inline long syscall_raw(long num, long a0, long a1, long a2, long a3, long a4, long a5)
{
    register long _a0 asm("a0") = a0;
    register long _a1 asm("a1") = a1;
    register long _a2 asm("a2") = a2;
    register long _a3 asm("a3") = a3;
    register long _a4 asm("a4") = a4;
    register long _a5 asm("a5") = a5;
    register long _a7 asm("a7") = num;
    
    asm volatile ("ecall" : "+r"(_a0) : "r"(_a1), "r"(_a2), "r"(_a3), "r"(_a4), "r"(_a5), "r"(_a7) : "memory");
    return _a0;
}

/* ==================== 用户态系统调用实现 ==================== */

void exit(int status) {
    syscall_raw(__NR_exit, status, 0, 0, 0, 0, 0);
    while(1); // 不应该到达这里
}

long write(int fd, const void *buf, size_t len) {
    return syscall_raw(__NR_write, fd, (long)buf, len, 0, 0, 0);
}

long read(int fd, void *buf, size_t count) {
    return syscall_raw(__NR_read, fd, (long)buf, count, 0, 0, 0);
}

void yield(void) {
    syscall_raw(__NR_yield, 0, 0, 0, 0, 0, 0);
}

int getpid(void) {
    return (int)syscall_raw(__NR_getpid, 0, 0, 0, 0, 0, 0);
}

int sleep(unsigned int seconds) {
    return (int)syscall_raw(__NR_sleep, seconds, 0, 0, 0, 0, 0);
}

/* ==================== Hart管理系统调用用户空间包装器 ==================== */

long hart_get_status(unsigned long hartid) {
    return syscall_raw(__NR_hart_get_status, hartid, 0, 0, 0, 0, 0);
}

int hart_count(void) {
    return (int)syscall_raw(__NR_hart_count, 0, 0, 0, 0, 0, 0);
}

long hart_current_id(void) {
    return syscall_raw(__NR_hart_current_id, 0, 0, 0, 0, 0, 0);
}

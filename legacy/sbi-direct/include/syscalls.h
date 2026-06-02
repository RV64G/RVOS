#ifndef _SYSCALLS_H
#define _SYSCALLS_H

#include "kernel/types.h"

/*
 * 极简系统调用架构
 * 
 * 添加新系统调用只需要3步：
 * 1. 在下面的 SYSCALL_LIST 中添加一行
 * 2. 在 kernel/syscall.c 中实现 do_函数名() 
 * 3. 重新编译
 */

// 系统调用定义列表 - 这是你唯一需要修改的地方
#define SYSCALL_LIST \
    SYSCALL(exit,   void, int status) \
    SYSCALL(write,  long, int fd, const void *buf, size_t len) \
    SYSCALL(read,   long, int fd, void *buf, size_t count) \
    SYSCALL(yield,  void) \
    SYSCALL(getpid, int) \
    SYSCALL(sleep,  int, unsigned int seconds) \
    SYSCALL(hart_get_status, long, unsigned long hartid) \
    SYSCALL(hart_count, int) \
    SYSCALL(hart_current_id, long) \
/* ===================== 自动生成部分 ===================== */

// 生成系统调用号
#define SYSCALL(name, ret_type, ...) __NR_##name,
enum { __NR_INVALID = 0, SYSCALL_LIST __NR_MAX };
#undef SYSCALL

// 生成用户态函数声明
#define SYSCALL(name, ret_type, ...) ret_type name(__VA_ARGS__);
SYSCALL_LIST
#undef SYSCALL

// 生成内核态函数声明  
#define SYSCALL(name, ret_type, ...) extern ret_type do_##name(__VA_ARGS__);
SYSCALL_LIST
#undef SYSCALL

// 内核系统调用分发函数
struct context;
extern void do_syscall(struct context *ctx);
extern void verify_syscall_table(void);

#endif /* _SYSCALLS_H */

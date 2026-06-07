#include "kernel.h"
#include "kernel/sched.h"
#include "kernel/printk.h"
#include "kernel/timer.h"
#include "string.h"
#include "riscv/sbi.h"
#include "syscalls.h"

/* ==================== 系统调用实现 ==================== */

void do_exit(int status)
{
    //printk("Task %d exiting with status %d\n", get_current_task_id(), status);
    task_exit(status);
}

#define MAX_WRITE_LEN 1024

long do_write(int fd, const void *buf, size_t len)
{
    if (fd != 1) {
        printk("sys_write: Invalid file descriptor %d.\n", fd);
        return -1;
    }
    if (buf == NULL) {
        printk("sys_write: Invalid user buffer (NULL).\n");
        return -1;
    }
    if (len > MAX_WRITE_LEN) {
        len = MAX_WRITE_LEN;
    }

    char k_buf[MAX_WRITE_LEN];
    memcpy(k_buf, buf, len);

    for (size_t i = 0; i < len; i++) {
        sbi_console_putchar(k_buf[i]);
    }
    return len;
}

long do_read(int fd, void *buf, size_t count)
{
    (void)fd; (void)buf; (void)count;
    return -1; // 暂未实现
}

void do_yield(void)
{
    schedule();
}

int do_getpid(void)
{
    return get_current_task_id();
}

int do_sleep(unsigned int seconds)
{
    //printk("do_sleep called with seconds=%d by task %d\n", seconds, get_current_task_id());
    
    if (seconds == 0) {
        return 0;
    }
    
    // 将秒转换为时钟周期数
    // TIMER_INTERVAL 定义为 10000000UL，表示大约每秒的时钟周期数
    // 所以 1 秒 = 1 个 timer tick (在 timer_create 的 timeout 参数中)
    uint32_t ticks = seconds;  // 直接使用秒数作为timer_create的timeout参数
    
    // 调用 task_delay 函数来让当前任务休眠
    task_delay(ticks);
    
    return 0;  // 成功返回0
}

/* ==================== Hart管理系统调用 ==================== */



/**
 * @brief 获取系统中Hart的数量
 * @return Hart数量
 */
int do_hart_count(void)
{
    return MAXNUM_CPU;
}

/**
 * @brief 获取当前Hart的ID
 * @return 当前Hart的ID
 */
long do_hart_current_id(void)
{
    return sbi_get_hartid();
}

/* ==================== 系统调用分发 ==================== */

// 生成系统调用表
#define SYSCALL(name, ret_type, ...) [__NR_##name] = (void*)do_##name,
static void* syscall_table[] = { [0] = NULL, SYSCALL_LIST };
#undef SYSCALL

// 添加验证函数
void verify_syscall_table(void)
{
    printk("=== Syscall Table Verification ===\n");
    printk("Table address: %p\n", syscall_table);
    printk("__NR_MAX = %d\n", __NR_MAX);
    
    for (int i = 0; i < __NR_MAX; i++) {
        printk("syscall_table[%d] = %p\n", i, syscall_table[i]);
    }
    
    // 验证函数地址
    printk("Function addresses:\n");
    printk("do_exit: %p\n", do_exit);
    printk("do_write: %p\n", do_write);
    printk("do_read: %p\n", do_read);
    printk("do_yield: %p\n", do_yield);
    printk("do_getpid: %p\n", do_getpid);
    printk("=== End Verification ===\n");
}

void do_syscall(struct context *ctx)
{
    uint32_t num = ctx->a7;
    
    // 添加调试信息
    // printk("DEBUG: syscall num=%d, table addr=%p\n", num, syscall_table);
    // printk("DEBUG: table[0]=%p, table[1]=%p, table[2]=%p\n", 
    //        syscall_table[0], syscall_table[1], syscall_table[2]);
    // printk("DEBUG: table[3]=%p, table[4]=%p, table[5]=%p\n", 
    //        syscall_table[3], syscall_table[4], syscall_table[5]);
    
    if (num <= 0 || num >= __NR_MAX || !syscall_table[num]) {
        printk("Unknown syscall no: %d.\n", num);
        ctx->a0 = -1;
        return;
    }
    
    // 统一调用接口（所有系统调用都用6个long参数）
    typedef long (*syscall_fn_t)(long, long, long, long, long, long);
    syscall_fn_t fn = (syscall_fn_t)syscall_table[num];
    
    ctx->a0 = fn(ctx->a0, ctx->a1, ctx->a2, ctx->a3, ctx->a4, ctx->a5);
}

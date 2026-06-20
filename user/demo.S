#include "syscall_numbers.h"

.section .user, "ax"
.globl user_demo_start
.globl user_demo_message_a
.globl user_demo_message_a_end
.globl user_demo_message_b
.globl user_demo_message_b_end
.globl user_demo_message_c
.globl user_demo_message_c_end
.align 2
user_demo_start:
    /*
     * 初始寄存器由内核任务创建时填好：
     *
     * a0 = demo task 编号
     * a1 = 每轮打印后的 sleep 毫秒数
     * a2 = 循环次数；0 表示一直循环
     *
     * 用户程序自己根据编号选择字符串；这样内核不再需要知道用户程序内部的字符串
     * 符号地址，后续替换成 ELF loader 时边界更清楚。
     */
    li t0, 1
    beq a0, t0, 3f
    li t0, 2
    beq a0, t0, 4f

    la s0, user_demo_message_a
    la s1, user_demo_message_a_end
    j 5f

3:
    la s0, user_demo_message_b
    la s1, user_demo_message_b_end
    j 5f

4:
    la s0, user_demo_message_c
    la s1, user_demo_message_c_end

5:
    sub s1, s1, s0
    mv s2, a1
    mv s3, a2
1:
    /*
     * write(fd=1, buf=s0, count=s1)
     *
     * 当前 syscall 号先按 Linux RISC-V ABI 放在 a7。write 的参数依次放在
     * a0/a1/a2：标准输出、用户缓冲区地址、字节数。内核会用 copy_from_user()
     * 把用户缓冲区分块复制出来，再交给 printk 输出。
     */
    li a0, 1
    mv a1, s0
    mv a2, s1
    li a7, SYS_WRITE
    ecall

    /*
     * sched_yield()
     *
     * 让当前用户 task 主动让出 CPU。内核不会在 syscall handler 里直接切栈，
     * 而是标记“trap 返回前需要调度”，最后由 trap.S 恢复另一个 task 的现场。
     */
    li a7, SYS_SCHED_YIELD
    ecall

    /*
     * sleep_ms(milliseconds=s2)
     *
     * 这是 RVOS 目前的临时演示 syscall，不是 Linux ABI。内核把当前 task 置为
     * BLOCKED，并把 task 内嵌的 sleep_timer 挂到 timer list；到期后 timer 回调
     * 会重新把它放回 READY 队列。
    */
    mv a0, s2
    li a7, SYS_SLEEP_MS
    ecall

    beqz s3, 1b
    addi s3, s3, -1
    bnez s3, 1b

    /*
     * exit(status=0)
     *
     * repeat_count 非 0 的 task 会走到这里。当前实现只把 task 标记成 ZOMBIE 并
     * 切走，还没有 wait/waitpid，也没有完整用户资源回收。
    */
    li a0, 0
    li a7, SYS_EXIT
    ecall

2:
    j 2b

user_demo_message_a:
    .ascii "user task A: write/yield/sleep\r\n"
user_demo_message_a_end:

user_demo_message_b:
    .ascii "user task B: shared code, private stack\r\n"
user_demo_message_b_end:

user_demo_message_c:
    .ascii "user task C: timer wakes blocked task\r\n"
user_demo_message_c_end:

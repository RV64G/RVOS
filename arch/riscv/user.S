.section .text.user_enter, "ax"
.globl riscv_enter_user
.align 2
riscv_enter_user:
    /*
     * a0 = user entry
     * a1 = user stack top
     * a2 = kernel trap stack top for traps from U-mode
     * a3 = initial user a0
     * a4 = initial user a1
     * a5 = initial user a2
     * a6 = initial user a3
     */
    csrw sscratch, a2
    csrw sepc, a0

    /*
     * sret 根据 sstatus.SPP 决定返回特权级。SPP=0 表示返回 U-mode；
     * SPIE=1 表示 sret 后用户态中断允许位会打开。
     */
    li t0, 0x100
    csrc sstatus, t0
    li t0, 0x20
    csrs sstatus, t0

    mv sp, a1
    mv a0, a3
    mv a1, a4
    mv a2, a5
    mv a3, a6
    sret

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
     * a0 = 本 task 要打印的用户态字符串地址
     * a1 = 字符串长度
     * a2 = 每轮打印后的 sleep 毫秒数
     * a3 = 循环次数；0 表示一直循环
     *
     * 这几个值保存在 s 寄存器里，后续 ecall 可以自由使用 a0-a7 传参。
     */
    mv s0, a0
    mv s1, a1
    mv s2, a2
    mv s3, a3
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
    li a7, 64
    ecall

    /*
     * sched_yield()
     *
     * 让当前用户 task 主动让出 CPU。内核不会在 syscall handler 里直接切栈，
     * 而是标记“trap 返回前需要调度”，最后由 trap.S 恢复另一个 task 的现场。
     */
    li a7, 124
    ecall

    /*
     * sleep_ms(milliseconds=s2)
     *
     * 这是 RVOS 目前的临时演示 syscall，不是 Linux ABI。内核把当前 task 置为
     * BLOCKED，并把 task 内嵌的 sleep_timer 挂到 timer list；到期后 timer 回调
     * 会重新把它放回 READY 队列。
     */
    mv a0, s2
    li a7, 1000
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
    li a7, 93
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

#include "syscall_numbers.h"

.section .text.start, "ax"
.globl _start
.align 2
_start:
    /*
     * a0 = demo task 编号
     * a1 = 首次打印前的 sleep 毫秒数，用来错开多个 demo task 的启动日志
     * a2 = 每轮打印后的 sleep 毫秒数
     * a3 = 循环次数；0 表示一直循环
     */
    li t0, 1
    beq a0, t0, 3f
    li t0, 2
    beq a0, t0, 4f

    la s0, message_a
    la s1, message_a_end
    j 5f

3:
    la s0, message_b
    la s1, message_b_end
    j 5f

4:
    la s0, message_c
    la s1, message_c_end

5:
    sub s1, s1, s0
    mv s2, a2
    mv s3, a3

    beqz a1, 1f
    mv a0, a1
    li a7, SYS_SLEEP_MS
    ecall
1:
    li a0, 1
    mv a1, s0
    mv a2, s1
    li a7, SYS_WRITE
    ecall

    li a7, SYS_SCHED_YIELD
    ecall

    mv a0, s2
    li a7, SYS_SLEEP_MS
    ecall

    beqz s3, 1b
    addi s3, s3, -1
    bnez s3, 1b

    li a0, 0
    li a7, SYS_EXIT
    ecall

2:
    j 2b

.section .rodata, "a"
message_a:
    .ascii "user ELF A: write/yield/sleep\r\n"
message_a_end:

message_b:
    .ascii "user ELF B: shared image, private address space\r\n"
message_b_end:

message_c:
    .ascii "user ELF C: exits after timer wakeups\r\n"
message_c_end:

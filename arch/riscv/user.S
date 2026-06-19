.section .text.user_enter, "ax"
.globl riscv_enter_user
.align 2
riscv_enter_user:
    /*
     * a0 = user entry
     * a1 = user stack top
     * a2 = kernel trap stack top for traps from U-mode
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
    sret

.section .user, "ax"
.globl user_demo_start
.align 2
user_demo_start:
    li a0, 1
    la a1, user_demo_message
    li a2, user_demo_message_end - user_demo_message
    li a7, 64
    ecall

    li a0, 0
    li a7, 93
    ecall

1:
    j 1b

user_demo_message:
    .ascii "Hello from U-mode syscall write\r\n"
user_demo_message_end:

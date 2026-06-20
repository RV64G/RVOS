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

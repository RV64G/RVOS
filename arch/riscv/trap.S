#include "trap_frame_offsets.h"

.section .text.trap, "ax"
.globl trap_vector
.align 2
trap_vector:
    /*
     * RISC-V 触发 trap 时只会写 sepc/scause/stval/sstatus 等 CSR，不会自动保存
     * 通用寄存器。S-mode 内核 trap 继续使用当前内核栈；U-mode trap 则通过
     * sscratch 切到内核准备好的 trap 栈，避免把 trap_frame 写到用户栈里。
    */
    csrrw sp, sscratch, sp
    bnez sp, .Lhave_kernel_sp

    /*
     * sscratch 为 0 表示 trap 发生在 S-mode。上面的 csrrw 临时把 sp 变成 0，
     * 再交换一次恢复原内核 sp，并保持 sscratch 为 0。
     */
    csrrw sp, sscratch, sp
.Lhave_kernel_sp:
    /*
     * 到这里时 sp 一定是内核可用栈：
     *
     * - S-mode trap：sp 是原内核栈，sscratch 为 0；
     * - U-mode trap：sp 是内核 trap 栈顶，sscratch 是被打断的用户 sp。
     */
    addi sp, sp, -TRAP_FRAME_SIZE

.Lsave_trap_frame:
    sd ra, TRAP_FRAME_RA(sp)
    sd t0, TRAP_FRAME_T0(sp)
    sd t1, TRAP_FRAME_T1(sp)
    /* t0 先记录 trap 前的内核 sp；如果来自 U-mode，下面再换成用户 sp。 */
    addi t0, sp, TRAP_FRAME_SIZE//把之前给trap加的补上
    csrr t1, sscratch
    beqz t1, .Lsave_sp
    mv t0, t1
.Lsave_sp:
    sd t0, TRAP_FRAME_SP(sp)
    sd gp, TRAP_FRAME_GP(sp)
    sd tp, TRAP_FRAME_TP(sp)
    sd t2, TRAP_FRAME_T2(sp)
    sd s0, TRAP_FRAME_S0(sp)
    sd s1, TRAP_FRAME_S1(sp)
    sd a0, TRAP_FRAME_A0(sp)
    sd a1, TRAP_FRAME_A1(sp)
    sd a2, TRAP_FRAME_A2(sp)
    sd a3, TRAP_FRAME_A3(sp)
    sd a4, TRAP_FRAME_A4(sp)
    sd a5, TRAP_FRAME_A5(sp)
    sd a6, TRAP_FRAME_A6(sp)
    sd a7, TRAP_FRAME_A7(sp)
    sd s2, TRAP_FRAME_S2(sp)
    sd s3, TRAP_FRAME_S3(sp)
    sd s4, TRAP_FRAME_S4(sp)
    sd s5, TRAP_FRAME_S5(sp)
    sd s6, TRAP_FRAME_S6(sp)
    sd s7, TRAP_FRAME_S7(sp)
    sd s8, TRAP_FRAME_S8(sp)
    sd s9, TRAP_FRAME_S9(sp)
    sd s10, TRAP_FRAME_S10(sp)
    sd s11, TRAP_FRAME_S11(sp)
    sd t3, TRAP_FRAME_T3(sp)
    sd t4, TRAP_FRAME_T4(sp)
    sd t5, TRAP_FRAME_T5(sp)
    sd t6, TRAP_FRAME_T6(sp)

    csrr t0, sepc
    sd t0, TRAP_FRAME_SEPC(sp)
    csrr t0, sstatus
    sd t0, TRAP_FRAME_SSTATUS(sp)
    csrr t0, scause
    sd t0, TRAP_FRAME_SCAUSE(sp)
    csrr t0, stval
    sd t0, TRAP_FRAME_STVAL(sp)

    mv a0, sp
    call trap_handle
    mv sp, a0

    /*
     * trap_handle() 返回要恢复的 trap_frame 地址。没有发生调度时就是原来的 sp；
     * 如果 timer interrupt 触发了调度，这里会切到下一个 task 的 trap_frame。
     * fatal trap 会在 C 侧停机，不会回到这条恢复路径。
     */
    ld t0, TRAP_FRAME_SEPC(sp)
    csrw sepc, t0
    ld t0, TRAP_FRAME_SSTATUS(sp)
    li t1, 0x100
    and t1, t0, t1 //查看spp位
    bnez t1, .Lrestore_kernel_scratch//不为用户态时跳转
    addi t1, sp, TRAP_FRAME_SIZE//为用户态时准备内核frame
    csrw sscratch, t1
    j .Lrestore_sstatus
.Lrestore_kernel_scratch:
    csrw sscratch, zero
.Lrestore_sstatus:
    csrw sstatus, t0

    ld ra, TRAP_FRAME_RA(sp)
    ld gp, TRAP_FRAME_GP(sp)
    ld tp, TRAP_FRAME_TP(sp)
    ld t0, TRAP_FRAME_T0(sp)
    ld t1, TRAP_FRAME_T1(sp)
    ld t2, TRAP_FRAME_T2(sp)
    ld s0, TRAP_FRAME_S0(sp)
    ld s1, TRAP_FRAME_S1(sp)
    ld a0, TRAP_FRAME_A0(sp)
    ld a1, TRAP_FRAME_A1(sp)
    ld a2, TRAP_FRAME_A2(sp)
    ld a3, TRAP_FRAME_A3(sp)
    ld a4, TRAP_FRAME_A4(sp)
    ld a5, TRAP_FRAME_A5(sp)
    ld a6, TRAP_FRAME_A6(sp)
    ld a7, TRAP_FRAME_A7(sp)
    ld s2, TRAP_FRAME_S2(sp)
    ld s3, TRAP_FRAME_S3(sp)
    ld s4, TRAP_FRAME_S4(sp)
    ld s5, TRAP_FRAME_S5(sp)
    ld s6, TRAP_FRAME_S6(sp)
    ld s7, TRAP_FRAME_S7(sp)
    ld s8, TRAP_FRAME_S8(sp)
    ld s9, TRAP_FRAME_S9(sp)
    ld s10, TRAP_FRAME_S10(sp)
    ld s11, TRAP_FRAME_S11(sp)
    ld t3, TRAP_FRAME_T3(sp)
    ld t4, TRAP_FRAME_T4(sp)
    ld t5, TRAP_FRAME_T5(sp)
    ld t6, TRAP_FRAME_T6(sp)
    ld sp, TRAP_FRAME_SP(sp)
    sret

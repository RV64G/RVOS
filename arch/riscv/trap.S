#include "trap_frame_offsets.h"

.section .text.trap, "ax"
.globl trap_vector
.align 2
trap_vector:
    /*
     * RISC-V 触发 trap 时只会写 sepc/scause/stval/sstatus 等 CSR，不会自动保存
     * 通用寄存器，也不会自动换栈。这里仍然使用当前内核栈，把 x1..x31 和关键
     * CSR 都压成一个 trap_frame，再交给 C 侧判断原因。
     */
    addi sp, sp, -TRAP_FRAME_SIZE

    sd ra, TRAP_FRAME_RA(sp)
    sd t0, TRAP_FRAME_T0(sp)
    addi t0, sp, TRAP_FRAME_SIZE
    sd t0, TRAP_FRAME_SP(sp)
    sd gp, TRAP_FRAME_GP(sp)
    sd tp, TRAP_FRAME_TP(sp)
    sd t1, TRAP_FRAME_T1(sp)
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

    /*
     * trap_handle() 的返回值放在 a0。当前汇编返回路径不根据它分支；fatal trap 会在
     * C 侧停机，handled trap 才会回到这里。随后恢复原始 a0，保证被中断现场不变。
     */
    ld t0, TRAP_FRAME_SEPC(sp)
    csrw sepc, t0
    ld t0, TRAP_FRAME_SSTATUS(sp)
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

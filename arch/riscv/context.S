#include "context_offsets.h"

.section .text.context, "ax"
.globl context_switch
.align 2
context_switch:
    /*
     * a0 = old context, a1 = next context。
     *
     * 这是普通内核函数调用边界上的上下文切换，不是 trap 返回。按 RISC-V C ABI，
     * 只需要保存 callee-saved 寄存器：ra、sp、s0-s11。a/t 寄存器本来就允许被调用
     * 者破坏，不属于长期任务上下文。
     */
    sd ra, CONTEXT_RA(a0)
    sd sp, CONTEXT_SP(a0)
    sd s0, CONTEXT_S0(a0)
    sd s1, CONTEXT_S1(a0)
    sd s2, CONTEXT_S2(a0)
    sd s3, CONTEXT_S3(a0)
    sd s4, CONTEXT_S4(a0)
    sd s5, CONTEXT_S5(a0)
    sd s6, CONTEXT_S6(a0)
    sd s7, CONTEXT_S7(a0)
    sd s8, CONTEXT_S8(a0)
    sd s9, CONTEXT_S9(a0)
    sd s10, CONTEXT_S10(a0)
    sd s11, CONTEXT_S11(a0)

    ld ra, CONTEXT_RA(a1)
    ld sp, CONTEXT_SP(a1)
    ld s0, CONTEXT_S0(a1)
    ld s1, CONTEXT_S1(a1)
    ld s2, CONTEXT_S2(a1)
    ld s3, CONTEXT_S3(a1)
    ld s4, CONTEXT_S4(a1)
    ld s5, CONTEXT_S5(a1)
    ld s6, CONTEXT_S6(a1)
    ld s7, CONTEXT_S7(a1)
    ld s8, CONTEXT_S8(a1)
    ld s9, CONTEXT_S9(a1)
    ld s10, CONTEXT_S10(a1)
    ld s11, CONTEXT_S11(a1)

    ret

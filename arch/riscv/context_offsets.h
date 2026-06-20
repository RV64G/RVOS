#ifndef ARCH_RISCV_CONTEXT_OFFSETS_H
#define ARCH_RISCV_CONTEXT_OFFSETS_H

/*
 * context_switch() 只保存函数调用边界必须保持的寄存器：
 * ra、sp 和 s0-s11。临时寄存器、参数寄存器由调用者按普通 C ABI 处理。
 */
#define CONTEXT_RA   0
#define CONTEXT_SP   8
#define CONTEXT_S0   16
#define CONTEXT_S1   24
#define CONTEXT_S2   32
#define CONTEXT_S3   40
#define CONTEXT_S4   48
#define CONTEXT_S5   56
#define CONTEXT_S6   64
#define CONTEXT_S7   72
#define CONTEXT_S8   80
#define CONTEXT_S9   88
#define CONTEXT_S10  96
#define CONTEXT_S11  104
#define CONTEXT_SIZE 112

#endif

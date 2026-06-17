#ifndef ARCH_RISCV_CONTEXT_H
#define ARCH_RISCV_CONTEXT_H

#include <stdint.h>

#include "compile_check.h"
#include "context_offsets.h"

struct context
{
    uint64_t ra;
    uint64_t sp;
    uint64_t s0;
    uint64_t s1;
    uint64_t s2;
    uint64_t s3;
    uint64_t s4;
    uint64_t s5;
    uint64_t s6;
    uint64_t s7;
    uint64_t s8;
    uint64_t s9;
    uint64_t s10;
    uint64_t s11;
};

CHECK_STRUCT_OFFSET(struct context, ra, CONTEXT_RA);
CHECK_STRUCT_OFFSET(struct context, sp, CONTEXT_SP);
CHECK_STRUCT_OFFSET(struct context, s0, CONTEXT_S0);
CHECK_STRUCT_OFFSET(struct context, s1, CONTEXT_S1);
CHECK_STRUCT_OFFSET(struct context, s2, CONTEXT_S2);
CHECK_STRUCT_OFFSET(struct context, s3, CONTEXT_S3);
CHECK_STRUCT_OFFSET(struct context, s4, CONTEXT_S4);
CHECK_STRUCT_OFFSET(struct context, s5, CONTEXT_S5);
CHECK_STRUCT_OFFSET(struct context, s6, CONTEXT_S6);
CHECK_STRUCT_OFFSET(struct context, s7, CONTEXT_S7);
CHECK_STRUCT_OFFSET(struct context, s8, CONTEXT_S8);
CHECK_STRUCT_OFFSET(struct context, s9, CONTEXT_S9);
CHECK_STRUCT_OFFSET(struct context, s10, CONTEXT_S10);
CHECK_STRUCT_OFFSET(struct context, s11, CONTEXT_S11);
CHECK_STRUCT_SIZE(struct context, CONTEXT_SIZE);

/**
 * 保存当前内核上下文并切换到 next。
 *
 * old 和 next 都指向 struct context。函数返回时，已经运行在 next 描述的内核栈上。
 * 之后某次切回 old 时，原调用点会继续向下执行。
 */
void context_switch(struct context *old, const struct context *next);

#endif

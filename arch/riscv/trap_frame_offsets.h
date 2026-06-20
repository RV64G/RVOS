#ifndef ARCH_RISCV_TRAP_FRAME_OFFSETS_H
#define ARCH_RISCV_TRAP_FRAME_OFFSETS_H

/*
 * trap.S 和 struct trap_frame 共用的布局约定。
 *
 * 汇编保存寄存器时只能使用常量偏移；C 侧会用 _Static_assert 校验这些常量是否仍然
 * 匹配 struct trap_frame。以后改 trap_frame 字段顺序时，必须同步更新这里。
 */
#define TRAP_FRAME_RA      0
#define TRAP_FRAME_SP      8
#define TRAP_FRAME_GP      16
#define TRAP_FRAME_TP      24
#define TRAP_FRAME_T0      32
#define TRAP_FRAME_T1      40
#define TRAP_FRAME_T2      48
#define TRAP_FRAME_S0      56
#define TRAP_FRAME_S1      64
#define TRAP_FRAME_A0      72
#define TRAP_FRAME_A1      80
#define TRAP_FRAME_A2      88
#define TRAP_FRAME_A3      96
#define TRAP_FRAME_A4      104
#define TRAP_FRAME_A5      112
#define TRAP_FRAME_A6      120
#define TRAP_FRAME_A7      128
#define TRAP_FRAME_S2      136
#define TRAP_FRAME_S3      144
#define TRAP_FRAME_S4      152
#define TRAP_FRAME_S5      160
#define TRAP_FRAME_S6      168
#define TRAP_FRAME_S7      176
#define TRAP_FRAME_S8      184
#define TRAP_FRAME_S9      192
#define TRAP_FRAME_S10     200
#define TRAP_FRAME_S11     208
#define TRAP_FRAME_T3      216
#define TRAP_FRAME_T4      224
#define TRAP_FRAME_T5      232
#define TRAP_FRAME_T6      240
#define TRAP_FRAME_SEPC    248
#define TRAP_FRAME_SSTATUS 256
#define TRAP_FRAME_SCAUSE  264
#define TRAP_FRAME_STVAL   272
#define TRAP_FRAME_RESERVED 280
#define TRAP_FRAME_SIZE    288

#endif

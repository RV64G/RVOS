#include "trap.h"

#include "compile_check.h"
#include "csr.h"
#include "early_log.h"
#include "printk.h"
#include "sched.h"
#include "trap_frame_offsets.h"
#include "timer.h"

#define SCAUSE_INTERRUPT (1ULL << 63)
#define SCAUSE_CODE_MASK (~SCAUSE_INTERRUPT)

#define SCAUSE_SUPERVISOR_SOFTWARE_INTERRUPT 1ULL
#define SCAUSE_SUPERVISOR_TIMER_INTERRUPT    5ULL
#define SCAUSE_SUPERVISOR_EXTERNAL_INTERRUPT 9ULL

#define SCAUSE_ILLEGAL_INSTRUCTION 2ULL
#define SCAUSE_BREAKPOINT          3ULL
#define SCAUSE_LOAD_ACCESS_FAULT   5ULL
#define SCAUSE_STORE_ACCESS_FAULT  7ULL
#define SCAUSE_USER_ECALL          8ULL
#define SCAUSE_SUPERVISOR_ECALL    9ULL
#define SCAUSE_INST_PAGE_FAULT     12ULL
#define SCAUSE_LOAD_PAGE_FAULT     13ULL
#define SCAUSE_STORE_PAGE_FAULT    15ULL

extern void trap_vector(void);

enum trap_result
{
    TRAP_HANDLED,
    TRAP_FATAL,
};

struct trap_outcome
{
    enum trap_result result;
    const char *reason;
};

CHECK_STRUCT_OFFSET(struct trap_frame, ra, TRAP_FRAME_RA);
CHECK_STRUCT_OFFSET(struct trap_frame, sp, TRAP_FRAME_SP);
CHECK_STRUCT_OFFSET(struct trap_frame, gp, TRAP_FRAME_GP);
CHECK_STRUCT_OFFSET(struct trap_frame, tp, TRAP_FRAME_TP);
CHECK_STRUCT_OFFSET(struct trap_frame, t0, TRAP_FRAME_T0);
CHECK_STRUCT_OFFSET(struct trap_frame, t1, TRAP_FRAME_T1);
CHECK_STRUCT_OFFSET(struct trap_frame, t2, TRAP_FRAME_T2);
CHECK_STRUCT_OFFSET(struct trap_frame, s0, TRAP_FRAME_S0);
CHECK_STRUCT_OFFSET(struct trap_frame, s1, TRAP_FRAME_S1);
CHECK_STRUCT_OFFSET(struct trap_frame, a0, TRAP_FRAME_A0);
CHECK_STRUCT_OFFSET(struct trap_frame, a1, TRAP_FRAME_A1);
CHECK_STRUCT_OFFSET(struct trap_frame, a2, TRAP_FRAME_A2);
CHECK_STRUCT_OFFSET(struct trap_frame, a3, TRAP_FRAME_A3);
CHECK_STRUCT_OFFSET(struct trap_frame, a4, TRAP_FRAME_A4);
CHECK_STRUCT_OFFSET(struct trap_frame, a5, TRAP_FRAME_A5);
CHECK_STRUCT_OFFSET(struct trap_frame, a6, TRAP_FRAME_A6);
CHECK_STRUCT_OFFSET(struct trap_frame, a7, TRAP_FRAME_A7);
CHECK_STRUCT_OFFSET(struct trap_frame, s2, TRAP_FRAME_S2);
CHECK_STRUCT_OFFSET(struct trap_frame, s3, TRAP_FRAME_S3);
CHECK_STRUCT_OFFSET(struct trap_frame, s4, TRAP_FRAME_S4);
CHECK_STRUCT_OFFSET(struct trap_frame, s5, TRAP_FRAME_S5);
CHECK_STRUCT_OFFSET(struct trap_frame, s6, TRAP_FRAME_S6);
CHECK_STRUCT_OFFSET(struct trap_frame, s7, TRAP_FRAME_S7);
CHECK_STRUCT_OFFSET(struct trap_frame, s8, TRAP_FRAME_S8);
CHECK_STRUCT_OFFSET(struct trap_frame, s9, TRAP_FRAME_S9);
CHECK_STRUCT_OFFSET(struct trap_frame, s10, TRAP_FRAME_S10);
CHECK_STRUCT_OFFSET(struct trap_frame, s11, TRAP_FRAME_S11);
CHECK_STRUCT_OFFSET(struct trap_frame, t3, TRAP_FRAME_T3);
CHECK_STRUCT_OFFSET(struct trap_frame, t4, TRAP_FRAME_T4);
CHECK_STRUCT_OFFSET(struct trap_frame, t5, TRAP_FRAME_T5);
CHECK_STRUCT_OFFSET(struct trap_frame, t6, TRAP_FRAME_T6);
CHECK_STRUCT_OFFSET(struct trap_frame, sepc, TRAP_FRAME_SEPC);
CHECK_STRUCT_OFFSET(struct trap_frame, sstatus, TRAP_FRAME_SSTATUS);
CHECK_STRUCT_OFFSET(struct trap_frame, scause, TRAP_FRAME_SCAUSE);
CHECK_STRUCT_OFFSET(struct trap_frame, stval, TRAP_FRAME_STVAL);
CHECK_STRUCT_OFFSET(struct trap_frame, reserved, TRAP_FRAME_RESERVED);
CHECK_STRUCT_SIZE(struct trap_frame, TRAP_FRAME_SIZE);
_Static_assert((TRAP_FRAME_SIZE % 16) == 0,
               "trap_frame size must preserve stack alignment");

static void print_trap_frame(const char *title, const struct trap_frame *frame)
{
    printk(title);
    printk("\r\n");
    printk_field("sepc", frame->sepc);
    printk_field("sstatus", frame->sstatus);
    printk_field("scause", frame->scause);
    printk_field("stval", frame->stval);
}

static void trap_stop(const char *reason, const struct trap_frame *frame)
{
    print_trap_frame(reason, frame);
    early_halt_forever();
}

static struct trap_outcome trap_handled(void)
{
    struct trap_outcome outcome = {
        .result = TRAP_HANDLED,
        .reason = 0,
    };
    return outcome;
}

static struct trap_outcome trap_fatal(const char *reason)
{
    struct trap_outcome outcome = {
        .result = TRAP_FATAL,
        .reason = reason,
    };
    return outcome;
}

void trap_init(void)
{
    /*
     * stvec 的低两位是模式位。这里传入自然对齐的函数地址，低两位为 0，
     * 表示 Direct 模式：所有异常和中断先进入同一个 trap_vector。
     */
    csr_write_stvec((uint64_t)trap_vector);
    printk("Trap vector installed\r\n");
}

static struct trap_outcome handle_interrupt(struct trap_frame *frame,
                                            uint64_t code)
{
    (void)frame;

    switch (code) {
    case SCAUSE_SUPERVISOR_SOFTWARE_INTERRUPT:
        return trap_fatal("Supervisor software interrupt");
    case SCAUSE_SUPERVISOR_TIMER_INTERRUPT:
        timer_handle_interrupt();
        return trap_handled();
    case SCAUSE_SUPERVISOR_EXTERNAL_INTERRUPT:
        return trap_fatal("Supervisor external interrupt");
    default:
        printk_field("interrupt_code", code);
        return trap_fatal("Unknown interrupt");
    }
}

static struct trap_outcome handle_exception(struct trap_frame *frame,
                                            uint64_t code)
{
    if (code == SCAUSE_BREAKPOINT) {
        /*
         * 当前只把 32-bit ebreak 当作调试断点处理，所以这里把 sepc 前进 4 字节。
         * 如果以后允许压缩指令触发 c.ebreak，需要根据指令长度决定前进 2 还是 4。
         */
        print_trap_frame("Breakpoint exception", frame);
        frame->sepc += 4;
        printk("Breakpoint trap returned\r\n");
        return trap_handled();
    }

    switch (code) {
    case SCAUSE_ILLEGAL_INSTRUCTION:
        return trap_fatal("Illegal instruction exception");
    case SCAUSE_LOAD_ACCESS_FAULT:
        return trap_fatal("Load access fault");
    case SCAUSE_STORE_ACCESS_FAULT:
        return trap_fatal("Store access fault");
    case SCAUSE_USER_ECALL:
        return trap_fatal("User ecall reached before syscall init");
    case SCAUSE_SUPERVISOR_ECALL:
        return trap_fatal("Supervisor ecall reached before syscall init");
    case SCAUSE_INST_PAGE_FAULT:
        return trap_fatal("Instruction page fault");
    case SCAUSE_LOAD_PAGE_FAULT:
        return trap_fatal("Load page fault");
    case SCAUSE_STORE_PAGE_FAULT:
        return trap_fatal("Store page fault");
    default:
        printk_field("exception_code", code);
        return trap_fatal("Unknown exception");
    }
}

struct trap_frame *trap_handle(struct trap_frame *frame)
{
    uint64_t code = frame->scause & SCAUSE_CODE_MASK;
    struct trap_outcome outcome;

    if ((frame->scause & SCAUSE_INTERRUPT) != 0) {
        outcome = handle_interrupt(frame, code);
    } else {
        outcome = handle_exception(frame, code);
    }

    if (outcome.result == TRAP_FATAL) {
        trap_stop(outcome.reason, frame);
    }

    return sched_from_trap(frame);
}

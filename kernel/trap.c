#include "trap.h"

#include <stddef.h>

#include "csr.h"
#include "early_log.h"
#include "printk.h"
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

struct trap_outcome
{
    enum trap_result result;
    const char *reason;
};

#define CHECK_TRAP_FRAME_OFFSET(field, offset) \
    _Static_assert(offsetof(struct trap_frame, field) == (offset), \
                   "trap_frame offset mismatch: " #field)

CHECK_TRAP_FRAME_OFFSET(ra, TRAP_FRAME_RA);
CHECK_TRAP_FRAME_OFFSET(sp, TRAP_FRAME_SP);
CHECK_TRAP_FRAME_OFFSET(gp, TRAP_FRAME_GP);
CHECK_TRAP_FRAME_OFFSET(tp, TRAP_FRAME_TP);
CHECK_TRAP_FRAME_OFFSET(t0, TRAP_FRAME_T0);
CHECK_TRAP_FRAME_OFFSET(t1, TRAP_FRAME_T1);
CHECK_TRAP_FRAME_OFFSET(t2, TRAP_FRAME_T2);
CHECK_TRAP_FRAME_OFFSET(s0, TRAP_FRAME_S0);
CHECK_TRAP_FRAME_OFFSET(s1, TRAP_FRAME_S1);
CHECK_TRAP_FRAME_OFFSET(a0, TRAP_FRAME_A0);
CHECK_TRAP_FRAME_OFFSET(a1, TRAP_FRAME_A1);
CHECK_TRAP_FRAME_OFFSET(a2, TRAP_FRAME_A2);
CHECK_TRAP_FRAME_OFFSET(a3, TRAP_FRAME_A3);
CHECK_TRAP_FRAME_OFFSET(a4, TRAP_FRAME_A4);
CHECK_TRAP_FRAME_OFFSET(a5, TRAP_FRAME_A5);
CHECK_TRAP_FRAME_OFFSET(a6, TRAP_FRAME_A6);
CHECK_TRAP_FRAME_OFFSET(a7, TRAP_FRAME_A7);
CHECK_TRAP_FRAME_OFFSET(s2, TRAP_FRAME_S2);
CHECK_TRAP_FRAME_OFFSET(s3, TRAP_FRAME_S3);
CHECK_TRAP_FRAME_OFFSET(s4, TRAP_FRAME_S4);
CHECK_TRAP_FRAME_OFFSET(s5, TRAP_FRAME_S5);
CHECK_TRAP_FRAME_OFFSET(s6, TRAP_FRAME_S6);
CHECK_TRAP_FRAME_OFFSET(s7, TRAP_FRAME_S7);
CHECK_TRAP_FRAME_OFFSET(s8, TRAP_FRAME_S8);
CHECK_TRAP_FRAME_OFFSET(s9, TRAP_FRAME_S9);
CHECK_TRAP_FRAME_OFFSET(s10, TRAP_FRAME_S10);
CHECK_TRAP_FRAME_OFFSET(s11, TRAP_FRAME_S11);
CHECK_TRAP_FRAME_OFFSET(t3, TRAP_FRAME_T3);
CHECK_TRAP_FRAME_OFFSET(t4, TRAP_FRAME_T4);
CHECK_TRAP_FRAME_OFFSET(t5, TRAP_FRAME_T5);
CHECK_TRAP_FRAME_OFFSET(t6, TRAP_FRAME_T6);
CHECK_TRAP_FRAME_OFFSET(sepc, TRAP_FRAME_SEPC);
CHECK_TRAP_FRAME_OFFSET(sstatus, TRAP_FRAME_SSTATUS);
CHECK_TRAP_FRAME_OFFSET(scause, TRAP_FRAME_SCAUSE);
CHECK_TRAP_FRAME_OFFSET(stval, TRAP_FRAME_STVAL);
CHECK_TRAP_FRAME_OFFSET(reserved, TRAP_FRAME_RESERVED);
_Static_assert(sizeof(struct trap_frame) == TRAP_FRAME_SIZE,
               "trap_frame size mismatch");
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

enum trap_result trap_handle(struct trap_frame *frame)
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

    return outcome.result;
}

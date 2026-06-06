#include "trap.h"

#include "csr.h"
#include "early_log.h"
#include "printk.h"
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

void trap_init(void)
{
    /*
     * stvec 的低两位是模式位。这里传入自然对齐的函数地址，低两位为 0，
     * 表示 Direct 模式：所有异常和中断先进入同一个 trap_vector。
     */
    csr_write_stvec((uint64_t)trap_vector);
    printk("Trap vector installed\r\n");
}

static void handle_interrupt(struct trap_frame *frame, uint64_t code)
{
    switch (code) {
    case SCAUSE_SUPERVISOR_SOFTWARE_INTERRUPT:
        trap_stop("Supervisor software interrupt", frame);
        return;
    case SCAUSE_SUPERVISOR_TIMER_INTERRUPT:
        timer_handle_interrupt();
        return;
    case SCAUSE_SUPERVISOR_EXTERNAL_INTERRUPT:
        trap_stop("Supervisor external interrupt", frame);
        return;
    default:
        printk_field("interrupt_code", code);
        trap_stop("Unknown interrupt", frame);
        return;
    }
}

static void handle_exception(struct trap_frame *frame, uint64_t code)
{
    if (code == SCAUSE_BREAKPOINT) {
        /*
         * 当前只把 32-bit ebreak 当作调试断点处理，所以这里把 sepc 前进 4 字节。
         * 如果以后允许压缩指令触发 c.ebreak，需要根据指令长度决定前进 2 还是 4。
         */
        print_trap_frame("Breakpoint exception", frame);
        frame->sepc += 4;
        printk("Breakpoint trap returned\r\n");
        return;
    }

    switch (code) {
    case SCAUSE_ILLEGAL_INSTRUCTION:
        trap_stop("Illegal instruction exception", frame);
        return;
    case SCAUSE_LOAD_ACCESS_FAULT:
        trap_stop("Load access fault", frame);
        return;
    case SCAUSE_STORE_ACCESS_FAULT:
        trap_stop("Store access fault", frame);
        return;
    case SCAUSE_USER_ECALL:
        trap_stop("User ecall reached before syscall init", frame);
        return;
    case SCAUSE_SUPERVISOR_ECALL:
        trap_stop("Supervisor ecall reached before syscall init", frame);
        return;
    case SCAUSE_INST_PAGE_FAULT:
        trap_stop("Instruction page fault", frame);
        return;
    case SCAUSE_LOAD_PAGE_FAULT:
        trap_stop("Load page fault", frame);
        return;
    case SCAUSE_STORE_PAGE_FAULT:
        trap_stop("Store page fault", frame);
        return;
    default:
        printk_field("exception_code", code);
        trap_stop("Unknown exception", frame);
        return;
    }
}

void trap_handle(struct trap_frame *frame)
{
    uint64_t code = frame->scause & SCAUSE_CODE_MASK;

    if ((frame->scause & SCAUSE_INTERRUPT) != 0) {
        handle_interrupt(frame, code);
        return;
    }

    handle_exception(frame, code);
}

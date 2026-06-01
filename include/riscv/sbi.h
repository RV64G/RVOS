#ifndef RISCV_SBI_H
#define RISCV_SBI_H

#include <stdint.h>

#define SBI_SET_TIMER                 0x00
#define SBI_CONSOLE_PUTCHAR           0x01
#define SBI_CONSOLE_GETCHAR           0x02
#define SBI_CLEAR_IPI                 0x03
#define SBI_SEND_IPI                  0x04
#define SBI_REMOTE_FENCE_I            0x05
#define SBI_REMOTE_SFENCE_VMA         0x06
#define SBI_REMOTE_SFENCE_VMA_ASID    0x07
#define SBI_SHUTDOWN                  0x08

#define SBI_EXT_HSM                   0x48534dUL
#define SBI_EXT_DBCN                  0x4442434eUL

#define SBI_HSM_HART_START            0x0
#define SBI_HSM_HART_STOP             0x1
#define SBI_HSM_HART_GET_STATUS       0x2
#define SBI_HSM_HART_SUSPEND          0x3

#define SBI_HSM_STATE_STARTED         0
#define SBI_HSM_STATE_STOPPED         1
#define SBI_HSM_STATE_START_PENDING   2
#define SBI_HSM_STATE_STOP_PENDING    3
#define SBI_HSM_STATE_SUSPENDED       4
#define SBI_HSM_STATE_SUSPEND_PENDING 5
#define SBI_HSM_STATE_RESUME_PENDING  6

#define SBI_DBCN_CONSOLE_WRITE_BYTE   2UL

#define SBI_SUCCESS                   0
#define SBI_ERR_FAILURE               -1
#define SBI_ERR_NOT_SUPPORTED         -2
#define SBI_ERR_INVALID_PARAM         -3
#define SBI_ERR_DENIED                -4
#define SBI_ERR_INVALID_ADDRESS       -5
#define SBI_ERR_ALREADY_AVAILABLE     -6
#define SBI_ERR_ALREADY_STARTED       -7
#define SBI_ERR_ALREADY_STOPPED       -8
#define SBI_ERR_NO_SHMEM              -9

struct sbiret {
    long error;
    long value;
};

static inline struct sbiret sbi_ext_call(
    long extension,
    long function,
    long arg0,
    long arg1,
    long arg2
)
{
    register long a0 __asm__("a0") = arg0;
    register long a1 __asm__("a1") = arg1;
    register long a2 __asm__("a2") = arg2;
    register long a6 __asm__("a6") = function;
    register long a7 __asm__("a7") = extension;

    __asm__ volatile (
        "ecall"
        : "+r"(a0), "+r"(a1)
        : "r"(a2), "r"(a6), "r"(a7)
        : "memory"
    );

    return (struct sbiret){
        .error = a0,
        .value = a1,
    };
}

static inline long sbi_legacy_call(long function, long arg0, long arg1, long arg2)
{
    register long a0 __asm__("a0") = arg0;
    register long a1 __asm__("a1") = arg1;
    register long a2 __asm__("a2") = arg2;
    register long a7 __asm__("a7") = function;

    __asm__ volatile (
        "ecall"
        : "+r"(a0)
        : "r"(a1), "r"(a2), "r"(a7)
        : "memory"
    );

    return a0;
}

static inline void sbi_legacy_console_putchar(int ch)
{
    sbi_legacy_call(SBI_CONSOLE_PUTCHAR, ch, 0, 0);
}

static inline int sbi_debug_console_putchar(int ch)
{
    struct sbiret ret = sbi_ext_call(
        SBI_EXT_DBCN,
        SBI_DBCN_CONSOLE_WRITE_BYTE,
        ch,
        0,
        0
    );

    return ret.error == SBI_SUCCESS;
}

static inline void sbi_console_putchar(int ch)
{
    if (!sbi_debug_console_putchar(ch)) {
        sbi_legacy_console_putchar(ch);
    }
}

static inline void sbi_console_puts(const char *s)
{
    while (*s) {
        sbi_console_putchar(*s);
        s++;
    }
}

static inline int sbi_console_getchar(void)
{
    return sbi_legacy_call(SBI_CONSOLE_GETCHAR, 0, 0, 0);
}

static inline void sbi_set_timer(uint64_t stime_value)
{
    sbi_legacy_call(SBI_SET_TIMER, stime_value, 0, 0);
}

static inline void sbi_shutdown(void)
{
    sbi_legacy_call(SBI_SHUTDOWN, 0, 0, 0);
}

static inline void sbi_clear_ipi(void)
{
    sbi_legacy_call(SBI_CLEAR_IPI, 0, 0, 0);
}

static inline void sbi_send_ipi(unsigned long hart_mask, unsigned long hart_mask_base)
{
    sbi_legacy_call(SBI_SEND_IPI, hart_mask, hart_mask_base, 0);
}

static inline uint64_t sbi_get_time(void)
{
    uint64_t time;
    __asm__ volatile ("rdtime %0" : "=r"(time));
    return time;
}

static inline long sbi_get_hartid(void)
{
    long hart_id;
    __asm__ volatile ("mv %0, tp" : "=r"(hart_id));
    return hart_id;
}

static inline struct sbiret sbi_hart_start(
    unsigned long hartid,
    unsigned long start_addr,
    unsigned long opaque
)
{
    return sbi_ext_call(SBI_EXT_HSM, SBI_HSM_HART_START, hartid, start_addr, opaque);
}

static inline struct sbiret sbi_hart_stop(void)
{
    return sbi_ext_call(SBI_EXT_HSM, SBI_HSM_HART_STOP, 0, 0, 0);
}

static inline struct sbiret sbi_hart_get_status(unsigned long hartid)
{
    return sbi_ext_call(SBI_EXT_HSM, SBI_HSM_HART_GET_STATUS, hartid, 0, 0);
}

static inline struct sbiret sbi_hart_suspend(
    unsigned long suspend_type,
    unsigned long resume_addr,
    unsigned long opaque
)
{
    return sbi_ext_call(
        SBI_EXT_HSM,
        SBI_HSM_HART_SUSPEND,
        suspend_type,
        resume_addr,
        opaque
    );
}

#endif

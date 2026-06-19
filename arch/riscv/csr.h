#ifndef ARCH_RISCV_CSR_H
#define ARCH_RISCV_CSR_H

#include <stdint.h>

static inline uint64_t csr_read_sstatus(void)
{
    uint64_t value;
    __asm__ volatile ("csrr %0, sstatus" : "=r"(value));
    return value;
}

static inline void csr_set_sstatus(uint64_t mask)
{
    __asm__ volatile ("csrs sstatus, %0" : : "r"(mask) : "memory");
}

static inline void csr_set_sie(uint64_t mask)
{
    __asm__ volatile ("csrs sie, %0" : : "r"(mask) : "memory");
}

static inline uint64_t csr_read_scause(void)
{
    uint64_t value;
    __asm__ volatile ("csrr %0, scause" : "=r"(value));
    return value;
}

static inline uint64_t csr_read_sepc(void)
{
    uint64_t value;
    __asm__ volatile ("csrr %0, sepc" : "=r"(value));
    return value;
}

static inline uint64_t csr_read_stval(void)
{
    uint64_t value;
    __asm__ volatile ("csrr %0, stval" : "=r"(value));
    return value;
}

static inline void csr_write_stvec(uint64_t value)
{
    __asm__ volatile ("csrw stvec, %0" : : "r"(value) : "memory");
}

static inline void csr_write_sscratch(uint64_t value)
{
    __asm__ volatile ("csrw sscratch, %0" : : "r"(value) : "memory");
}

#endif

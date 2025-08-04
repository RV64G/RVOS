#ifndef __RISCV_H__
#define __RISCV_H__

#include "kernel/types.h"

// Forward declaration to avoid circular dependency
uint64_t get_saved_hartid(void);

/*
 * ref: https://github.com/mit-pdos/xv6-riscv/blob/riscv/kernel/riscv.h
 */

static inline reg_t r_tp()
{
	reg_t x;
	asm volatile("mv %0, tp" : "=r" (x) );
	return x;
}

/* which hart (core) is this? - Read mhartid CSR directly */
static inline reg_t r_mhartid()
{
	reg_t x;
	asm volatile("csrr %0, mhartid" : "=r" (x));
	return x;
}

/* Machine Status Register, mstatus */
#define MSTATUS_MPP (3 << 11)
#define MSTATUS_SPP (1 << 8)

#define MSTATUS_MPIE (1 << 7)
#define MSTATUS_SPIE (1 << 5)
#define MSTATUS_UPIE (1 << 4)

#define MSTATUS_MIE (1 << 3)
#define MSTATUS_SIE (1 << 1)
#define MSTATUS_UIE (1 << 0)

static inline reg_t r_mstatus()
{
	reg_t x;
	asm volatile("csrr %0, mstatus" : "=r" (x) );
	return x;
}

static inline void w_mstatus(reg_t x)
{
	asm volatile("csrw mstatus, %0" : : "r" (x));
}

/*
 * machine exception program counter, holds the
 * instruction address to which a return from
 * exception will go.
 */
static inline void w_mepc(reg_t x)
{
	asm volatile("csrw mepc, %0" : : "r" (x));
}

static inline reg_t r_mepc()
{
	reg_t x;
	asm volatile("csrr %0, mepc" : "=r" (x));
	return x;
}

/* Machine Scratch register, for early trap handler */
static inline void w_mscratch(reg_t x)
{
	asm volatile("csrw mscratch, %0" : : "r" (x));
}

/* Machine-mode interrupt vector */
static inline void w_mtvec(reg_t x)
{
	asm volatile("csrw mtvec, %0" : : "r" (x));
}

/* Machine-mode Interrupt Enable */
#define MIE_MEIE (1 << 11) // external
#define MIE_MTIE (1 << 7)  // timer
#define MIE_MSIE (1 << 3)  // software

static inline reg_t r_mie()
{
	reg_t x;
	asm volatile("csrr %0, mie" : "=r" (x) );
	return x;
}

static inline void w_mie(reg_t x)
{
	asm volatile("csrw mie, %0" : : "r" (x));
}

static inline reg_t r_mcause()
{
	reg_t x;
	asm volatile("csrr %0, mcause" : "=r" (x) );
	return x;
}

/* Supervisor Status Register, sstatus - S-mode equivalent of mstatus */
#define SSTATUS_SPP (1 << 8)   // Supervisor Previous Privilege
#define SSTATUS_SPIE (1 << 5)  // Supervisor Previous Interrupt Enable
#define SSTATUS_SIE (1 << 1)   // Supervisor Interrupt Enable

static inline reg_t r_sstatus()
{
	reg_t x;
	asm volatile("csrr %0, sstatus" : "=r" (x) );
	return x;
}

static inline void w_sstatus(reg_t x)
{
	asm volatile("csrw sstatus, %0" : : "r" (x));
}

/* Supervisor Exception Program Counter */
static inline void w_sepc(reg_t x)
{
	asm volatile("csrw sepc, %0" : : "r" (x));
}

static inline reg_t r_sepc()
{
	reg_t x;
	asm volatile("csrr %0, sepc" : "=r" (x) );
	return x;
}

/* S-mode tvec register */
static inline void w_stvec(reg_t x)
{
	asm volatile("csrw stvec, %0" : : "r" (x));
}

/* S-mode scratch register */
static inline void w_sscratch(reg_t x)
{
	asm volatile("csrw sscratch, %0" : : "r" (x));
}

/* Supervisor Interrupt Enable */
static inline reg_t r_sie()
{
	reg_t x;
	asm volatile("csrr %0, sie" : "=r" (x) );
	return x;
}

static inline void w_sie(reg_t x)
{
	asm volatile("csrw sie, %0" : : "r" (x));
}

/* S-mode interrupt enable bits */
#define SIE_SEIE (1 << 9)  /* Supervisor external interrupt enable */
#define SIE_STIE (1 << 5)  /* Supervisor timer interrupt enable */
#define SIE_SSIE (1 << 1)  /* Supervisor software interrupt enable */

/* Supervisor Cause Register */
static inline reg_t r_scause()
{
	reg_t x;
	asm volatile("csrr %0, scause" : "=r" (x) );
	return x;
}

/* Supervisor Trap Value */
static inline reg_t r_stval()
{
	reg_t x;
	asm volatile("csrr %0, stval" : "=r" (x) );
	return x;
}

/* Hart ID through tp register (set by OpenSBI in start.S) */
static inline reg_t r_hartid()
{
	reg_t x;
	asm volatile("mv %0, tp" : "=r" (x) );
	return x;
}

#define SCHEDULE do { \
    extern void schedule(void); \
    schedule(); \
} while(0);

extern void disable_pmp(void);

/* S-mode CSR access macros */
#define w_stvec(x) asm volatile("csrw stvec, %0" : : "r" (x))
#define w_sscratch(x) asm volatile("csrw sscratch, %0" : : "r" (x))

#define read_csr_sie() ({ \
    reg_t __tmp; \
    asm volatile("csrr %0, sie" : "=r" (__tmp)); \
    __tmp; \
})

#define write_csr_sie(x) asm volatile("csrw sie, %0" : : "r" (x))

// Rename the inline functions to avoid conflicts
#define r_sie() read_csr_sie()
#define w_sie(x) write_csr_sie(x)

// Interrupt control

// push_off/pop_off are like intr_off()/intr_on() except that they are matched:
// it takes two pop_off()s to undo two push_off()s. Also, if interrupts
// are initially off, then push_off, pop_off leaves them off.

static inline reg_t push_off()
{
	reg_t old_sstatus = r_sstatus();
	// clear SIE bit
	w_sstatus(old_sstatus & ~SSTATUS_SIE);
	return old_sstatus;
}

static inline void pop_off(reg_t old_sstatus)
{
	w_sstatus(old_sstatus);
}

// Provides a hint to the CPU that we are in a spin-wait loop.
static inline void spin_loop_hint()
{
	// The 'pause' instruction is a hint to the CPU that the code is in a
	// spin-wait loop. This can help the CPU save power and improve performance
	// by yielding execution resources to other hyper-threads.
	// RISC-V has a 'pause' instruction in the Zihintpause extension.
	// For now, we use a compiler memory barrier as a placeholder.
	__asm__ volatile ("" ::: "memory");
}

#endif /* __RISCV_H__ */

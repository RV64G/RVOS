#ifndef __PLATFORM_H__
#define __PLATFORM_H__

/*
 * QEMU RISC-V Virt machine with 16550a UART and VirtIO MMIO
 */

/* 
 * maximum number of CPUs
 * see https://github.com/qemu/qemu/blob/master/include/hw/riscv/virt.h
 * #define VIRT_CPUS_MAX 8
 */
#define MAXNUM_CPU 8

/*
 * MemoryMap
 * see https://github.com/qemu/qemu/blob/master/hw/riscv/virt.c, virt_memmap[] 
 * 0x00001000 -- boot ROM, provided by qemu
 * 0x02000000 -- CLINT
 * 0x0C000000 -- PLIC
 * 0x10000000 -- UART0
 * 0x10001000 -- virtio disk
 * 0x80000000 -- boot ROM jumps here in machine mode, where we load our kernel
 */

/* This machine puts UART registers here in physical memory. */
#define UART0 0x10000000L

/*
 * UART0 interrupt source
 * see https://github.com/qemu/qemu/blob/master/include/hw/riscv/virt.h
 * enum {
 *     UART0_IRQ = 10,
 *     ......
 * };
 */
#define UART0_IRQ 10

/*
 * This machine puts platform-level interrupt controller (PLIC) here.
 * For S-mode, we need to use S-mode context registers instead of M-mode.
 * QEMU virt machine PLIC configuration:
 * - Hart 0 M-mode context: 0x200000
 * - Hart 0 S-mode context: 0x201000  
 * - Hart 1 M-mode context: 0x202000
 * - Hart 1 S-mode context: 0x203000
 * Pattern: M-mode context = 0x200000 + hart_id * 0x2000
 *          S-mode context = 0x201000 + hart_id * 0x2000
 */
#define PLIC_BASE 0x0c000000L
#define PLIC_PRIORITY(id) (PLIC_BASE + (id) * 4)
#define PLIC_PENDING(id) (PLIC_BASE + 0x1000 + ((id) / 32) * 4)

/* M-mode PLIC registers (keep for reference) */
#define PLIC_MENABLE(hart) (PLIC_BASE + 0x2000 + (hart) * 0x80)
#define PLIC_MTHRESHOLD(hart) (PLIC_BASE + 0x200000 + (hart) * 0x1000)
#define PLIC_MCLAIM(hart) (PLIC_BASE + 0x200004 + (hart) * 0x1000)
#define PLIC_MCOMPLETE(hart) (PLIC_BASE + 0x200004 + (hart) * 0x1000)

/* S-mode PLIC registers (what we should use in S-mode) */
#define PLIC_SENABLE(hart) (PLIC_BASE + 0x2080 + (hart) * 0x80)
#define PLIC_STHRESHOLD(hart) (PLIC_BASE + 0x201000 + (hart) * 0x1000)  
#define PLIC_SCLAIM(hart) (PLIC_BASE + 0x201004 + (hart) * 0x1000)
#define PLIC_SCOMPLETE(hart) (PLIC_BASE + 0x201004 + (hart) * 0x1000)

 /*
  * The Core Local INTerruptor (CLINT) block holds memory-mapped control and
  * status registers associated with software and timer interrupts.
  * 
  * NOTE: In S-mode, we should use SBI calls instead of direct CLINT access.
  * These definitions are kept for reference and compatibility.
  * 
  * QEMU-virt reuses sifive configuration for CLINT.
  * see https://gitee.com/qemu/qemu/blob/master/include/hw/riscv/sifive_clint.h
  */
#define CLINT_BASE 0x2000000L
/* Legacy M-mode CLINT registers - use SBI calls instead */
#define CLINT_MSIP(hartid) (CLINT_BASE + 4 * (hartid))
#define CLINT_MTIMECMP(hartid) (CLINT_BASE + 0x4000 + 8 * (hartid))
#define CLINT_MTIME (CLINT_BASE + 0xBFF8) // cycles since boot.

/* 10000000 ticks per-second */
#define CLINT_TIMEBASE_FREQ 10000000

#endif /* __PLATFORM_H__ */

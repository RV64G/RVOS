#include "kernel.h"
#include "riscv/sbi.h"
#include "kernel/sched.h"  // 包含调度器头文件，获取extern声明

extern void trap_vector(void);
extern void uart_isr(void);
extern void timer_handler(void);
extern void schedule(void);
extern void do_syscall(struct context *ctx);

extern int current_ctx;
struct context context_inited = {0};

void trap_init()
{	/*
	 * set the trap-vector base-address for supervisor-mode
	 */
	asm volatile("csrw stvec, %0" : : "r" ((reg_t)trap_vector));
	asm volatile("csrw sscratch, %0" : : "r" ((reg_t)&context_inited));
}

// void external_interrupt_handler()
// {
// 	int irq = plic_claim();

// 	if (irq == UART0_IRQ)
// 	{
// 		uart_isr();
// 	}
// 	else if (irq)
// 	{
// 		printk("unexpected interrupt irq = %d\n", irq);
// 	}

// 	if (irq)
// 	{
// 		plic_complete(irq);
// 	}
// }

/**
 * @brief 内核的中心陷阱处理器
 * @details
 *   当任何异常、中断或系统调用发生时，CPU硬件会强制跳转到 `arch/riscv/entry.S` 中的 `trap_vector`。
 *   `trap_vector` 保存当前上下文后，会调用此函数进行处理。
 *   此函数通过分析 `cause` 寄存器来区分陷阱类型，并分发给相应的处理函数。
 * 
 * @param epc 发生陷阱时CPU的程序计数器 (PC)
 * @param cause 描述陷阱原因的控制寄存器
 * @param ctx 指向被中断任务的上下文保存区域的指针
 * @return 返回给 `trap_vector` 的PC值，通常是 `epc` 或 `epc + 4`
 */
reg_t trap_handler(reg_t epc, reg_t cause, struct context *ctx)
{	reg_t return_pc = epc;
	reg_t cause_code = cause & 0xfff;
	//printk("trap_handler\n");
	if (cause & 0x8000000000000000ULL) // 64-bit interrupt flag
	{
		// 异步陷阱：中断处理
		switch (cause_code)
		{
		case 1: // Supervisor software interrupt
		{
			/* In S-mode, clear the software interrupt via SBI */
			sbi_clear_ipi();
			schedule();
			break;
		}
		case 5: // Supervisor timer interrupt
			timer_handler();
			break;
		case 9: // Supervisor external interrupt
			//external_interrupt_handler();
			break;
		default:
			printk("未知的异步异常！\n");
			break;
		}
	}
	else
	{
		// 同步异常
		switch (cause_code)
		{
		case 2:
			printk("Illegal instruction!\n");
			printk("PC: 0x%lx, Cause: 0x%lx\n", epc, cause);
			printk("Current task ID: %d\n", current_task_id);
			if (current_task_id >= 0) {
				printk("Task state: %d\n", tasks[current_task_id].state);
			}
			while (1);
			break;
		case 5:
			printk("Fault load!\n");
			while(1);
			break;
		case 7:
			printk("Fault store!\n");
			while(1);
			break;
		case 8:
			///printk("Environment call from U-mode!\n");
			ctx->pc = epc + 4;
			do_syscall(ctx);
			return_pc = ctx->pc;
			break;
		case 11:
			//printk("Environment call from M-mode!\n");
			ctx->pc = epc + 4;
			do_syscall(ctx);
			return_pc = ctx->pc;
			break;
		default:
			/* Synchronous trap - exception */
			printk("Sync exceptions! cause code: %d\n", cause_code);
			while (1)
			{
				;
			}
			
			break;
		}
	}
	return return_pc;
}

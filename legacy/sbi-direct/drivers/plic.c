#include "kernel.h"

void plic_init(void)
{
	int hart = r_tp();
  
	/* 
	 * Set priority for UART0.
	 *
	 * Each PLIC interrupt source can be assigned a priority by writing 
	 * to its 32-bit memory-mapped priority register.
	 * The QEMU-virt (the same as FU540-C000) supports 7 levels of priority. 
	 * A priority value of 0 is reserved to mean "never interrupt" and 
	 * effectively disables the interrupt. 
	 * Priority 1 is the lowest active priority, and priority 7 is the highest. 
	 * Ties between global interrupts of the same priority are broken by 
	 * the Interrupt ID; interrupts with the lowest ID have the highest 
	 * effective priority.
	 */
	*(uint32_t*)PLIC_PRIORITY(UART0_IRQ) = 1;
 
	/*
	 * Enable UART0 for S-mode context
	 * Use S-mode enable register instead of M-mode
	 */
	*(uint32_t*)PLIC_SENABLE(hart) = (1 << UART0_IRQ);

	/* 
	 * Set priority threshold for UART0 in S-mode context
	 * Use S-mode threshold register instead of M-mode
	 */
	*(uint32_t*)PLIC_STHRESHOLD(hart) = 0;
	
	/* enable supervisor-mode external interrupts. */
	w_sie(r_sie() | SIE_SEIE);

	/* enable supervisor-mode global interrupts. */
	w_sstatus(r_sstatus() | SSTATUS_SIE);
}

/* 
 * DESCRIPTION:
 *	Query the PLIC what interrupt we should serve.
 *	Perform an interrupt claim by reading the claim register, which
 *	returns the ID of the highest-priority pending interrupt or zero if there 
 *	is no pending interrupt. 
 *	A successful claim also atomically clears the corresponding pending bit
 *	on the interrupt source.
 * RETURN VALUE:
 *	the ID of the highest-priority pending interrupt or zero if there 
 *	is no pending interrupt.
 */
int plic_claim(void)
{
	int hart = r_tp();
	/* Use S-mode claim register */
	int irq = *(uint32_t*)PLIC_SCLAIM(hart);
	return irq;
}

/* 
 * DESCRIPTION:
  *	Writing the interrupt ID it received from the claim (irq) to the 
 *	complete register would signal the PLIC we've served this IRQ. 
 *	The PLIC does not check whether the completion ID is the same as the 
 *	last claim ID for that target. If the completion ID does not match an 
 *	interrupt source that is currently enabled for the target, the completion
 *	is silently ignored.
 * RETURN VALUE: none
 */
void plic_complete(int irq)
{
	int hart = r_tp();
	/* Use S-mode complete register */
	*(uint32_t*)PLIC_SCOMPLETE(hart) = irq;
}

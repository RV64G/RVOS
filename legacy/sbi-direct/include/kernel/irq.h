#ifndef __KERNEL_IRQ_H__
#define __KERNEL_IRQ_H__

extern void plic_init(void);
extern int plic_claim(void);
extern void plic_complete(int irq);
extern void trap_init(void);

#endif /* __KERNEL_IRQ_H__ */

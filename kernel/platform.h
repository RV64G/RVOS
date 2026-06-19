#ifndef KERNEL_PLATFORM_H
#define KERNEL_PLATFORM_H

#include <stdint.h>

struct platform_info {
    const char *model;
    uint64_t boot_hart_id;
    uint64_t timebase_frequency;
    uint64_t uart_base;
    uint64_t uart_size;
    uint32_t uart_reg_shift;
    uint32_t uart_reg_io_width;
    uint32_t uart_irq;
    uint64_t irq_base;
    uint64_t irq_size;
    uint64_t irq_context;
    int has_irq_context;
};

void platform_info_reset(uint64_t boot_hart_id);
const struct platform_info *platform_info(void);
struct platform_info *platform_info_mut(void);
void platform_info_print(void);
int platform_map_mmio(void);

#endif

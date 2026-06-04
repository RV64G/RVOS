#include "platform.h"

#include "early_log.h"

static struct platform_info current_platform;

void platform_info_reset(uint64_t boot_hart_id)
{
    current_platform.model = 0;
    current_platform.boot_hart_id = boot_hart_id;
    current_platform.timebase_frequency = 0;
    current_platform.uart_base = 0;
    current_platform.uart_size = 0;
    current_platform.irq_base = 0;
    current_platform.irq_size = 0;
}

const struct platform_info *platform_info(void)
{
    return &current_platform;
}

struct platform_info *platform_info_mut(void)
{
    return &current_platform;
}

void platform_info_print(void)
{
    early_puts("Platform info\r\n");
    early_print_field("boot_hart_id", current_platform.boot_hart_id);

    if (current_platform.model) {
        early_puts("  model: ");
        early_puts(current_platform.model);
        early_puts("\r\n");
    }

    early_print_dec_field("timebase_frequency", current_platform.timebase_frequency);
    early_print_field("uart_base", current_platform.uart_base);
    early_print_field("uart_size", current_platform.uart_size);
    early_print_field("irq_base", current_platform.irq_base);
    early_print_field("irq_size", current_platform.irq_size);
}

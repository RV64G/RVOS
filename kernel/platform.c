#include "platform.h"

#include "early_log.h"
#include "early_vm.h"
#include "vm.h"

static struct platform_info current_platform;

static int map_mmio_range(const char *name, uint64_t base, uint64_t size)
{
    if (base == 0 || size == 0) {
        return 1;
    }

    /*
     * 设备 MMIO 不属于 EFI conventional memory，所以不会被 early_vm_enable() 的
     * 内存恒等映射覆盖。DTB 解析完成后，正式访问 UART/PLIC 前必须把这些 range
     * 追加到当前内核页表。
     */
    if (!vm_identity_map(kernel_vm_space(), base, size, VM_MAP_READ | VM_MAP_WRITE)) {
        early_puts("Platform MMIO mapping failed\r\n");
        early_puts("  device: ");
        early_puts(name);
        early_puts("\r\n");
        early_print_field("base", base);
        early_print_field("size", size);
        return 0;
    }

    return 1;
}

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

int platform_map_mmio(void)
{
    if (!map_mmio_range("uart", current_platform.uart_base, current_platform.uart_size)) {
        return 0;
    }
    if (!map_mmio_range("irq", current_platform.irq_base, current_platform.irq_size)) {
        return 0;
    }

    vm_flush_all();
    return 1;
}

#include "dtb.h"

#include <stdint.h>

#include "early_log.h"
#include "libfdt.h"
#include "platform.h"

/*
 * DTB 解析分成两层：
 *
 * - libfdt 负责 FDT 二进制格式本身：header 校验、节点遍历、属性查找、字符串表处理。
 * - 本文件只负责从设备树里抽取内核当前关心的平台信息，并写入 platform_info。
 *
 * 这样后续继续解析 watchdog、framebuffer、更多 UART 变体时，不需要再维护一套
 * 手写 token parser。
 */
/*
 * DTB 的 reg 属性描述设备地址窗口。当前 UART/PLIC 只需要第一个 range，所以这里用
 * 一个小结构在解析函数和保存函数之间传递“基址 + 长度”。
 */
struct dtb_reg_range {
    uint64_t addr;
    uint64_t size;
};

static uint32_t boot_cpu_intc_phandle;

/* 在 compatible 这种字符串列表里做宽松匹配，例如 "snps,dw-apb-uart" 包含 "uart"。 */
static int str_contains(const char *s, const char *needle)
{
    if (!*needle)
    {
        return 1;
    }

    for (; *s; s++)
    {
        const char *a = s;
        const char *b = needle;
        while (*a && *b && *a == *b)
        {
            a++;
            b++;
        }
        if (!*b)
        {
            return 1;
        }
    }

    return 0;
}

static int compatible_contains(const void *fdt, int node, const char *needle)
{
    int len = 0;
    const char *compatible = fdt_getprop(fdt, node, "compatible", &len);
    int offset = 0;

    while (compatible && offset < len)
    {
        const char *item = compatible + offset;
        int item_len = 0;

        while (offset + item_len < len && item[item_len] != '\0')
        {
            item_len++;
        }

        if (str_contains(item, needle))
        {
            return 1;
        }

        offset += item_len + 1;
    }

    return 0;
}

/* 读取一个 32-bit 属性并转换成 CPU 端序。属性缺失或长度不足时返回 0。 */
static int get_u32_prop(const void *fdt, int node, const char *name,
                        uint32_t *value)
{
    int len = 0;
    const fdt32_t *prop = fdt_getprop(fdt, node, name, &len);

    if (!prop || len < (int)sizeof(*prop))
    {
        return 0;
    }

    *value = fdt32_to_cpu(*prop);
    return 1;
}

/*
 * #address-cells/#size-cells 决定子节点 reg 属性里地址和长度各占几个 32-bit cell。
 * 没写时使用 devicetree 规范里的默认值：address=2, size=1。
 */
static int get_address_cells(const void *fdt, int node)
{
    uint32_t value;

    if (!get_u32_prop(fdt, node, "#address-cells", &value))
    {
        return 2;
    }
    return value <= 4 ? (int)value : -1;
}

static int get_size_cells(const void *fdt, int node)
{
    uint32_t value;

    if (!get_u32_prop(fdt, node, "#size-cells", &value))
    {
        return 1;
    }
    return value <= 4 ? (int)value : -1;
}

/* 把连续的大端 32-bit cell 拼成一个整数，例如 <0x0 0x10000000> -> 0x10000000。 */
static uint64_t read_cells(const fdt32_t *cells, int count)
{
    uint64_t value = 0;

    for (int i = 0; i < count; i++)
    {
        value = (value << 32) | fdt32_to_cpu(cells[i]);
    }

    return value;
}

/*
 * 解析 node 的第一个 reg range。
 *
 * reg 的格式由父节点的 #address-cells/#size-cells 决定，所以这里必须先读 parent。
 * 多 range 设备后续再扩展；当前 UART 和 PLIC 只依赖第一个 MMIO 窗口。
 */
static int parse_first_reg(const void *fdt, int node, struct dtb_reg_range *range)
{
    int parent = fdt_parent_offset(fdt, node);
    int address_cells;
    int size_cells;
    int cells;
    int len = 0;
    const fdt32_t *reg;

    if (parent < 0)
    {
        return 0;
    }

    address_cells = get_address_cells(fdt, parent);
    size_cells = get_size_cells(fdt, parent);
    if (address_cells < 0 || size_cells < 0)
    {
        return 0;
    }

    cells = address_cells + size_cells;
    reg = fdt_getprop(fdt, node, "reg", &len);
    if (!reg || cells == 0 || len < cells * (int)sizeof(fdt32_t))
    {
        return 0;
    }

    range->addr = read_cells(reg, address_cells);
    range->size = read_cells(reg + address_cells, size_cells);
    return 1;
}

/*
 * 找到启动 hart 在 PLIC 里的 S-mode external interrupt context。
 *
 * interrupts-extended 是一串 phandle/interrupt-id 对。先在第一轮扫描中记录 boot hart
 * 的 cpu-intc phandle，再在 PLIC 节点里找 interrupt-id 9 的那一项。该项下标就是后续
 * claim/complete 寄存器使用的 PLIC context number。
 */
static void parse_plic_context(const void *fdt, int node)
{
    int len = 0;
    const fdt32_t *cells;
    int pairs;
    struct platform_info *platform;

    if (boot_cpu_intc_phandle == 0)
    {
        return;
    }

    cells = fdt_getprop(fdt, node, "interrupts-extended", &len);
    if (!cells || (len % (2 * (int)sizeof(fdt32_t))) != 0)
    {
        return;
    }

    pairs = len / (2 * (int)sizeof(fdt32_t));
    platform = platform_info_mut();

    /*
     * PLIC 的 interrupts-extended 是 phandle/interrupt-id 列表。RISC-V
     * cpu-intc 的 interrupt id 9 表示 S-mode external interrupt。匹配 boot hart
     * 的 cpu-intc phandle 后，pair 序号就是 PLIC context number。
     */
    for (int i = 0; i < pairs; i++)
    {
        uint32_t phandle = fdt32_to_cpu(cells[i * 2]);
        uint32_t interrupt_id = fdt32_to_cpu(cells[i * 2 + 1]);

        if (phandle == boot_cpu_intc_phandle && interrupt_id == 9U)
        {
            platform->irq_context = (uint64_t)i;
            platform->has_irq_context = 1;
            return;
        }
    }
}

static void scan_boot_cpu_intc(const void *fdt, int node)
{
    int parent;
    struct dtb_reg_range cpu_reg;
    struct platform_info *platform = platform_info_mut();

    if (!compatible_contains(fdt, node, "riscv,cpu-intc"))
    {
        return;
    }

    parent = fdt_parent_offset(fdt, node);
    if (parent < 0 || !parse_first_reg(fdt, parent, &cpu_reg))
    {
        return;
    }

    if (cpu_reg.addr == platform->boot_hart_id)
    {
        boot_cpu_intc_phandle = fdt_get_phandle(fdt, node);
    }
}

/* 保存第一个可用 UART 节点。这里先只抽取最小串口驱动需要的字段。 */
static void save_uart_if_needed(const void *fdt, int node)
{
    uint32_t value;
    struct dtb_reg_range reg;
    struct platform_info *platform = platform_info_mut();

    if (platform->uart_base != 0)
    {
        return;
    }

    if (!compatible_contains(fdt, node, "ns16550") &&
        !compatible_contains(fdt, node, "uart") &&
        !compatible_contains(fdt, node, "serial"))
    {
        return;
    }

    if (!parse_first_reg(fdt, node, &reg))
    {
        return;
    }

    platform->uart_base = reg.addr;
    platform->uart_size = reg.size;

    if (get_u32_prop(fdt, node, "reg-shift", &value))
    {
        platform->uart_reg_shift = value;
    }
    if (get_u32_prop(fdt, node, "reg-io-width", &value))
    {
        platform->uart_reg_io_width = value;
    }
    if (get_u32_prop(fdt, node, "interrupts", &value))
    {
        platform->uart_irq = value;
    }
}

/* 保存第一个中断控制器 MMIO 窗口，并顺手解析 PLIC context。 */
static void save_interrupt_controller_if_needed(const void *fdt, int node)
{
    struct dtb_reg_range reg;
    struct platform_info *platform = platform_info_mut();

    if (platform->irq_base != 0)
    {
        return;
    }

    if (!compatible_contains(fdt, node, "plic") &&
        !compatible_contains(fdt, node, "aia") &&
        !compatible_contains(fdt, node, "interrupt-controller"))
    {
        return;
    }

    if (!parse_first_reg(fdt, node, &reg))
    {
        return;
    }

    platform->irq_base = reg.addr;
    platform->irq_size = reg.size;
    parse_plic_context(fdt, node);
}

/* 抽取不依赖其它节点的信息：板卡 model 和 timer timebase。 */
static void scan_common_properties(const void *fdt, int node)
{
    int len = 0;
    uint32_t timebase;
    const char *model;
    struct platform_info *platform = platform_info_mut();

    if (node == 0)
    {
        model = fdt_getprop(fdt, node, "model", &len);
        if (model && len > 0)
        {
            platform->model = model;
        }
    }

    if (platform->timebase_frequency == 0 &&
        get_u32_prop(fdt, node, "timebase-frequency", &timebase))
    {
        platform->timebase_frequency = timebase;
    }
}

/*
 * 两轮扫描是为了处理 phandle 依赖：
 *
 * - 第 0 轮先找 boot hart 对应的 cpu-intc phandle。
 * - 第 1 轮再解析 UART/PLIC。PLIC context 需要用到上一轮记录的 phandle。
 */
static void scan_nodes(const void *fdt, int pass)
{
    int node = -1;
    int depth = -1;

    while ((node = fdt_next_node(fdt, node, &depth)) >= 0)
    {
        if (pass == 0)
        {
            scan_common_properties(fdt, node);
            scan_boot_cpu_intc(fdt, node);
        }
        else
        {
            save_uart_if_needed(fdt, node);
            save_interrupt_controller_if_needed(fdt, node);
        }
    }
}

int dtb_init(const struct kernel_boot_info *boot_info)
{
    const void *fdt;
    int status;

    platform_info_reset(boot_info->boot_hart_id);
    boot_cpu_intc_phandle = 0;

    if ((boot_info->flags & KERNEL_BOOT_HAS_DTB) == 0 ||
        boot_info->dtb_phys == 0)
    {
        early_puts("DTB: not available\r\n");
        platform_info_print();
        return 0;
    }

    fdt = (const void *)(uintptr_t)boot_info->dtb_phys;

    early_puts("DTB parse start\r\n");
    early_print_field("dtb_phys", boot_info->dtb_phys);
    early_print_field("dtb_size", boot_info->dtb_size);

    status = fdt_check_header(fdt);
    if (status != 0)
    {
        early_puts("DTB rejected: ");
        early_puts(fdt_strerror(status));
        early_puts("\r\n");
        platform_info_print();
        return 0;
    }

    if (fdt_totalsize(fdt) == 0 || fdt_totalsize(fdt) > boot_info->dtb_size)
    {
        early_puts("DTB rejected: size outside boot info range\r\n");
        platform_info_print();
        return 0;
    }

    early_print_field("fdt_magic", fdt_magic(fdt));
    early_print_field("fdt_totalsize", fdt_totalsize(fdt));

    scan_nodes(fdt, 0);
    scan_nodes(fdt, 1);

    early_puts("DTB parse done\r\n");
    platform_info_print();
    return 1;
}

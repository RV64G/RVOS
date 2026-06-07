#include "dtb.h"

#include <stdint.h>

#include "early_log.h"
#include "platform.h"

#define FDT_MAGIC 0xd00dfeedU

#define FDT_BEGIN_NODE 1U
#define FDT_END_NODE   2U
#define FDT_PROP       3U
#define FDT_NOP        4U
#define FDT_END        9U

#define FDT_MAX_DEPTH 32

/*
 * 第一版 DTB 解析器只做早期硬件发现。它运行在正式 UART/printk 之前，所以输出走
 * early_log，也就是 SBI console。解析结果写入 platform_info，供后续 UART、timer
 * 和中断控制器初始化使用。
 */
struct fdt_header {
    uint32_t magic;
    uint32_t totalsize;
    uint32_t off_dt_struct;
    uint32_t off_dt_strings;
    uint32_t off_mem_rsvmap;
    uint32_t version;
    uint32_t last_comp_version;
    uint32_t boot_cpuid_phys;
    uint32_t size_dt_strings;
    uint32_t size_dt_struct;
};

struct fdt_node_state {
    const char *name;
    uint32_t address_cells;
    uint32_t size_cells;
    uint32_t parent_address_cells;
    uint32_t parent_size_cells;
    uint64_t reg_addr;
    uint64_t reg_size;
    uint32_t uart_reg_shift;
    uint32_t uart_reg_io_width;
    int has_reg;
    int is_uart;
    int is_interrupt_controller;
};

static uint32_t be32_read(const void *ptr)
{
    const uint8_t *p = (const uint8_t *)ptr;
    return ((uint32_t)p[0] << 24) |
           ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) |
           (uint32_t)p[3];
}

static uint64_t be_cells_read(const uint32_t *cells, uint32_t count)
{
    uint64_t value = 0;

    for (uint32_t i = 0; i < count; i++) {
        value = (value << 32) | be32_read(&cells[i]);
    }

    return value;
}

static uint32_t align4(uint32_t value)
{
    return (value + 3U) & ~3U;
}

static int str_eq(const char *a, const char *b)
{
    while (*a && *b && *a == *b) {
        a++;
        b++;
    }

    return *a == *b;
}

static int str_contains(const char *s, const char *needle)
{
    if (!*needle) {
        return 1;
    }

    for (; *s; s++) {
        const char *a = s;
        const char *b = needle;
        while (*a && *b && *a == *b) {
            a++;
            b++;
        }
        if (!*b) {
            return 1;
        }
    }

    return 0;
}

static int compatible_has(const char *value, uint32_t len, const char *needle)
{
    uint32_t offset = 0;

    while (offset < len) {
        const char *item = value + offset;
        uint32_t item_len = 0;

        while (offset + item_len < len && item[item_len] != '\0') {
            item_len++;
        }

        if (str_contains(item, needle)) {
            return 1;
        }

        offset += item_len + 1;
    }

    return 0;
}

static int range_inside(uint64_t total_size, uint64_t offset, uint64_t size)
{
    return offset <= total_size && size <= total_size - offset;
}

static void save_reg_if_needed(const struct fdt_node_state *node)
{
    struct platform_info *platform = platform_info_mut();

    if (!node->has_reg) {
        return;
    }

    if (node->is_uart && platform->uart_base == 0) {
        platform->uart_base = node->reg_addr;
        platform->uart_size = node->reg_size;
        platform->uart_reg_shift = node->uart_reg_shift;
        platform->uart_reg_io_width = node->uart_reg_io_width;
    }

    if (node->is_interrupt_controller && platform->irq_base == 0) {
        platform->irq_base = node->reg_addr;
        platform->irq_size = node->reg_size;
    }
}

static void parse_reg(struct fdt_node_state *node, const void *value, uint32_t len)
{
    /*
     * reg 属性的 cell 数由父节点决定。当前只取第一个 reg range，足够定位
     * UART/PLIC MMIO 基址。多 range 设备后续再扩展 platform_info 表达。
     */
    uint32_t cells = node->parent_address_cells + node->parent_size_cells;
    const uint32_t *cell_values = (const uint32_t *)value;

    if (cells == 0 || len < cells * sizeof(uint32_t)) {
        return;
    }

    node->reg_addr = be_cells_read(cell_values, node->parent_address_cells);
    node->reg_size = be_cells_read(
        cell_values + node->parent_address_cells,
        node->parent_size_cells
    );
    node->has_reg = 1;
}

static void parse_property(
    struct fdt_node_state *node,
    const char *prop_name,
    const void *value,
    uint32_t len,
    int depth
)
{
    struct platform_info *platform = platform_info_mut();

    if (str_eq(prop_name, "#address-cells") && len >= sizeof(uint32_t)) {
        node->address_cells = be32_read(value);
        return;
    }

    if (str_eq(prop_name, "#size-cells") && len >= sizeof(uint32_t)) {
        node->size_cells = be32_read(value);
        return;
    }

    if (str_eq(prop_name, "model") && depth == 0) {
        platform->model = (const char *)value;
        return;
    }

    if (str_eq(prop_name, "compatible")) {
        if (compatible_has((const char *)value, len, "ns16550") ||
            compatible_has((const char *)value, len, "uart") ||
            compatible_has((const char *)value, len, "serial")) {
            node->is_uart = 1;
        }
        if (compatible_has((const char *)value, len, "plic") ||
            compatible_has((const char *)value, len, "aia") ||
            compatible_has((const char *)value, len, "interrupt-controller")) {
            node->is_interrupt_controller = 1;
        }
        return;
    }

    if (str_eq(prop_name, "timebase-frequency") && len >= sizeof(uint32_t)) {
        platform->timebase_frequency = be32_read(value);
        return;
    }

    /*
     * ns16550 兼容 UART 的寄存器编号是 THR=0、LSR=5，但某些 SoC 会把寄存器按
     * 32-bit 间隔摆放。DTB 用 reg-shift 表示“寄存器编号左移几位后才是字节偏移”：
     * reg-shift=2 时，LSR 实际在 base + (5 << 2)。
     */
    if (str_eq(prop_name, "reg-shift") && len >= sizeof(uint32_t)) {
        node->uart_reg_shift = be32_read(value);
        return;
    }

    /*
     * reg-io-width 表示访问寄存器时应使用的总线宽度。没有这个属性时按传统 8-bit
     * ns16550 处理；StarFive 一类平台常见的是 reg-shift=2、reg-io-width=4。
     */
    if (str_eq(prop_name, "reg-io-width") && len >= sizeof(uint32_t)) {
        node->uart_reg_io_width = be32_read(value);
        return;
    }

    if (str_eq(prop_name, "reg")) {
        parse_reg(node, value, len);
    }
}

static int parse_structure_block(
    const uint8_t *struct_block,
    const char *strings,
    uint32_t size_dt_struct,
    uint32_t size_dt_strings
)
{
    /*
     * structure block 是一串大端 32-bit token，不是普通文本。解析方式类似递归
     * 遍历设备树，但这里用 stack[] 保存当前路径，避免早期阶段引入递归和动态分配。
     *
     * 主要 token 形态：
     *   BEGIN_NODE, 节点名, 若干 PROP/子节点, END_NODE
     *   PROP, 属性长度, 属性名在 strings block 中的偏移, 属性值
     *   END 表示整棵树结束
     */
    uint32_t pos = 0;
    int depth = -1;
    struct fdt_node_state stack[FDT_MAX_DEPTH];

    while (pos + sizeof(uint32_t) <= size_dt_struct) {
        uint32_t token = be32_read(struct_block + pos);
        pos += sizeof(uint32_t);

        if (token == FDT_BEGIN_NODE) {
            if (depth + 1 >= FDT_MAX_DEPTH) {
                early_puts("DTB rejected: node depth too large\r\n");
                return 0;
            }

            /*
             * BEGIN_NODE 后面紧跟一个 '\0' 结尾的节点名，然后补齐到 4 字节边界。
             * 根节点名字为空字符串，这里显示成 "/" 方便后续调试。
             */
            const char *name = (const char *)(struct_block + pos);
            uint32_t name_len = 0;
            while (pos + name_len < size_dt_struct && name[name_len] != '\0') {
                name_len++;
            }
            if (pos + name_len >= size_dt_struct) {
                early_puts("DTB rejected: unterminated node name\r\n");
                return 0;
            }

            pos = align4(pos + name_len + 1);
            depth++;

            /*
             * reg 属性的地址/长度 cell 数由父节点定义。进入新节点时先继承父节点的
             * #address-cells/#size-cells；如果当前节点自己定义了这两个属性，
             * parse_property() 会更新 stack[depth]，供它的子节点继续继承。
             */
            uint32_t inherited_address_cells = 2;
            uint32_t inherited_size_cells = 1;
            if (depth > 0) {
                inherited_address_cells = stack[depth - 1].address_cells;
                inherited_size_cells = stack[depth - 1].size_cells;
            }

            stack[depth].name = (*name == '\0') ? "/" : name;
            stack[depth].address_cells = inherited_address_cells;
            stack[depth].size_cells = inherited_size_cells;
            stack[depth].parent_address_cells = inherited_address_cells;
            stack[depth].parent_size_cells = inherited_size_cells;
            stack[depth].reg_addr = 0;
            stack[depth].reg_size = 0;
            stack[depth].uart_reg_shift = 0;
            stack[depth].uart_reg_io_width = 1;
            stack[depth].has_reg = 0;
            stack[depth].is_uart = 0;
            stack[depth].is_interrupt_controller = 0;
            continue;
        }

        if (token == FDT_END_NODE) {
            if (depth < 0) {
                early_puts("DTB rejected: unexpected END_NODE\r\n");
                return 0;
            }
            /*
             * 一个节点结束时，它的 compatible/reg 等属性都已经读完。此时才能判断
             * “这是 UART/PLIC 节点，而且已经拿到 MMIO range”，并写入 platform_info。
             */
            save_reg_if_needed(&stack[depth]);
            depth--;
            continue;
        }

        if (token == FDT_PROP) {
            if (depth < 0 || pos + 8 > size_dt_struct) {
                early_puts("DTB rejected: bad property header\r\n");
                return 0;
            }

            uint32_t len = be32_read(struct_block + pos);
            uint32_t nameoff = be32_read(struct_block + pos + 4);
            pos += 8;

            if (pos + len > size_dt_struct || nameoff >= size_dt_strings) {
                early_puts("DTB rejected: bad property range\r\n");
                return 0;
            }

            /*
             * 属性值在 structure block 内，属性名不直接存这里，而是通过 nameoff 指到
             * strings block。value 长度也要按 4 字节对齐后再读取下一个 token。
             */
            const void *value = struct_block + pos;
            const char *prop_name = strings + nameoff;
            pos = align4(pos + len);

            parse_property(&stack[depth], prop_name, value, len, depth);
            continue;
        }

        if (token == FDT_NOP) {
            continue;
        }

        if (token == FDT_END) {
            return 1;
        }

        early_puts("DTB rejected: unknown token\r\n");
        early_print_field("token", token);
        return 0;
    }

    early_puts("DTB rejected: missing END token\r\n");
    return 0;
}

int dtb_init(const struct kernel_boot_info *boot_info)
{
    platform_info_reset(boot_info->boot_hart_id);

    if ((boot_info->flags & KERNEL_BOOT_HAS_DTB) == 0 ||
        boot_info->dtb_phys == 0 ||
        boot_info->dtb_size < sizeof(struct fdt_header)) {
        early_puts("DTB: not available\r\n");
        platform_info_print();
        return 0;
    }

    const uint8_t *dtb = (const uint8_t *)(uintptr_t)boot_info->dtb_phys;
    const struct fdt_header *header = (const struct fdt_header *)dtb;

    uint32_t magic = be32_read(&header->magic);
    uint32_t totalsize = be32_read(&header->totalsize);
    uint32_t off_dt_struct = be32_read(&header->off_dt_struct);
    uint32_t off_dt_strings = be32_read(&header->off_dt_strings);
    uint32_t size_dt_struct = be32_read(&header->size_dt_struct);
    uint32_t size_dt_strings = be32_read(&header->size_dt_strings);

    early_puts("DTB parse start\r\n");
    early_print_field("dtb_phys", boot_info->dtb_phys);
    early_print_field("dtb_size", boot_info->dtb_size);
    early_print_field("fdt_magic", magic);
    early_print_field("fdt_totalsize", totalsize);

    if (magic != FDT_MAGIC) {
        early_puts("DTB rejected: bad magic\r\n");
        platform_info_print();
        return 0;
    }

    if (totalsize == 0 || totalsize > boot_info->dtb_size) {
        early_puts("DTB rejected: size outside boot info range\r\n");
        platform_info_print();
        return 0;
    }

    if (!range_inside(totalsize, off_dt_struct, size_dt_struct) ||
        !range_inside(totalsize, off_dt_strings, size_dt_strings)) {
        early_puts("DTB rejected: block outside totalsize\r\n");
        platform_info_print();
        return 0;
    }

    if (!parse_structure_block(
            dtb + off_dt_struct,
            (const char *)(dtb + off_dt_strings),
            size_dt_struct,
            size_dt_strings)) {
        platform_info_print();
        return 0;
    }

    early_puts("DTB parse done\r\n");
    platform_info_print();
    return 1;
}

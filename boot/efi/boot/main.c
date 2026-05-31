typedef unsigned long long uint64_t;
typedef unsigned int uint32_t;
typedef unsigned long long uintptr_t;
typedef unsigned long long UINTN;
typedef unsigned long long EFI_STATUS;
/* EFI_HANDLE 是固件维护的不透明句柄，程序不能假设它的内部结构。 */
typedef void *EFI_HANDLE;
typedef unsigned short CHAR16;

#define EFI_SUCCESS 0
/* UEFI 错误码会把最高位置 1，低位才是具体错误编号。 */
#define EFI_ERROR(status) (0x8000000000000000ULL | (status))
#define EFI_BUFFER_TOO_SMALL EFI_ERROR(5)

/*
 * RISC-V EFI 镜像必须带一个 .reloc 数据目录。当前程序没有真正需要重定位的
 * 项目，但一些固件/加载器会拒绝完全没有 .reloc 的 PE/COFF，所以这里放一个
 * 空的 base relocation block。
 */
__attribute__((section(".reloc"), used))
static const unsigned int base_reloc_block[] = { 0, 8 };

/* 所有 UEFI 表开头都有这个公共头，用来描述表签名、版本和大小。 */
typedef struct {
    uint64_t signature;
    uint32_t revision;
    uint32_t header_size;
    uint32_t crc32;
    uint32_t reserved;
} efi_table_header_t;

typedef struct {
    uint32_t data1;
    unsigned short data2;
    unsigned short data3;
    unsigned char data4[8];
} efi_guid_t;

typedef struct efi_simple_text_output_protocol efi_simple_text_output_protocol_t;

/* 这里只声明当前阶段会用到的控制台输出函数，其他成员先按指针占位。 */
struct efi_simple_text_output_protocol {
    void *reset;
    EFI_STATUS (*output_string)(
        efi_simple_text_output_protocol_t *this,
        CHAR16 *string
    );
};

typedef struct {
    efi_guid_t vendor_guid;
    void *vendor_table;
} efi_configuration_table_t;

typedef struct efi_boot_services efi_boot_services_t;

/*
 * Boot Services 是退出 EFI 前能调用的服务表。这里按 UEFI 规范顺序声明到
 * GetMemoryMap 为止，后面的服务当前不用，暂时不展开。
 */
struct efi_boot_services {
    efi_table_header_t hdr;
    void *raise_tpl;
    void *restore_tpl;
    EFI_STATUS (*allocate_pages)();
    EFI_STATUS (*free_pages)();
    EFI_STATUS (*get_memory_map)(
        UINTN *memory_map_size,
        void *memory_map,
        UINTN *map_key,
        UINTN *descriptor_size,
        uint32_t *descriptor_version
    );
};

typedef struct {
    uint32_t type;
    uint32_t padding;
    uint64_t physical_start;
    uint64_t virtual_start;
    uint64_t number_of_pages;
    uint64_t attribute;
} efi_memory_descriptor_t;

/*
 * EFI 固件把 system table 传给 efi_main。它是 EFI 启动层的根对象：
 * 控制台、Boot Services、Runtime Services、configuration table 都从这里拿。
 */
typedef struct {
    efi_table_header_t hdr;
    CHAR16 *firmware_vendor;
    uint32_t firmware_revision;
    EFI_HANDLE console_in_handle;
    void *con_in;
    EFI_HANDLE console_out_handle;
    efi_simple_text_output_protocol_t *con_out;
    void *standard_error_handle;
    efi_simple_text_output_protocol_t *std_err;
    void *runtime_services;
    efi_boot_services_t *boot_services;
    UINTN number_of_table_entries;
    efi_configuration_table_t *configuration_table;
} efi_system_table_t;

/* EFI configuration table 里用于标识 DTB 的标准 GUID。 */
static const efi_guid_t efi_dtb_table_guid = {
    0xb1b621d5, 0xf19c, 0x41a5,
    { 0x83, 0x0b, 0xd9, 0x15, 0x2c, 0x69, 0xaa, 0xe0 }
};

static int guid_eq(const efi_guid_t *a, const efi_guid_t *b)
{
    if (a->data1 != b->data1 || a->data2 != b->data2 || a->data3 != b->data3) {
        return 0;
    }

    for (int i = 0; i < 8; i++) {
        if (a->data4[i] != b->data4[i]) {
            return 0;
        }
    }

    return 1;
}

/* FDT/DTB 文件头按大端存储，RISC-V 主机侧是小端，所以读头字段要转换。 */
static uint32_t be32_to_cpu(uint32_t value)
{
    return ((value & 0xff000000U) >> 24) |
           ((value & 0x00ff0000U) >> 8) |
           ((value & 0x0000ff00U) << 8) |
           ((value & 0x000000ffU) << 24);
}

static void puts(efi_system_table_t *st, const CHAR16 *s)
{
    st->con_out->output_string(st->con_out, (CHAR16 *)s);
}

static void print_hex64(efi_system_table_t *st, uint64_t value)
{
    CHAR16 buf[] = {
        '0', 'x',
        '0', '0', '0', '0', '0', '0', '0', '0',
        '0', '0', '0', '0', '0', '0', '0', '0',
        0
    };

    for (int i = 0; i < 16; i++) {
        unsigned int shift = (15 - i) * 4;
        unsigned int digit = (value >> shift) & 0xf;
        buf[2 + i] = (digit < 10) ? (CHAR16)('0' + digit)
                                  : (CHAR16)('a' + digit - 10);
    }

    puts(st, buf);
}

static void print_u64(efi_system_table_t *st, uint64_t value)
{
    CHAR16 buf[21];
    int pos = 20;

    buf[pos] = 0;
    if (value == 0) {
        buf[--pos] = '0';
    } else {
        while (value != 0 && pos > 0) {
            buf[--pos] = (CHAR16)('0' + (value % 10));
            value /= 10;
        }
    }

    puts(st, &buf[pos]);
}

/* U-Boot 会把 DTB 注册进 EFI configuration table，不需要手动把 FDT 地址传进来。 */
static void *find_dtb(efi_system_table_t *st)
{
    for (UINTN i = 0; i < st->number_of_table_entries; i++) {
        efi_configuration_table_t *entry = &st->configuration_table[i];
        if (guid_eq(&entry->vendor_guid, &efi_dtb_table_guid)) {
            return entry->vendor_table;
        }
    }

    return 0;
}

static void print_dtb_info(efi_system_table_t *st)
{
    uint32_t *dtb = (uint32_t *)find_dtb(st);

    puts(st, L"DTB: ");
    if (!dtb) {
        puts(st, L"missing\r\n");
        return;
    }

    uint32_t magic = be32_to_cpu(dtb[0]);
    uint32_t total_size = be32_to_cpu(dtb[1]);

    puts(st, L"found at ");
    print_hex64(st, (uint64_t)(uintptr_t)dtb);
    puts(st, L", magic=");
    print_hex64(st, magic);
    puts(st, L", size=");
    print_u64(st, total_size);
    puts(st, L"\r\n");
}

/*
 * 第一次调用 GetMemoryMap 时传空 buffer，固件会返回 EFI_BUFFER_TOO_SMALL，
 * 同时把需要的 buffer 大小和单条 descriptor 大小写回来。下一阶段会真的分配
 * buffer，把完整 memory map 保存进 boot_info，再 ExitBootServices。
 */
static void print_memory_map_info(efi_system_table_t *st)
{
    UINTN map_size = 0;
    UINTN map_key = 0;
    UINTN desc_size = 0;
    uint32_t desc_version = 0;

    EFI_STATUS status = st->boot_services->get_memory_map(
        &map_size, 0, &map_key, &desc_size, &desc_version
    );

    puts(st, L"Memory map: ");
    if (status != EFI_BUFFER_TOO_SMALL || desc_size == 0) {
        puts(st, L"query failed, status=");
        print_hex64(st, status);
        puts(st, L"\r\n");
        return;
    }

    puts(st, L"size=");
    print_u64(st, map_size);
    puts(st, L", desc_size=");
    print_u64(st, desc_size);
    puts(st, L", entries=");
    print_u64(st, map_size / desc_size);
    puts(st, L", version=");
    print_u64(st, desc_version);
    puts(st, L"\r\n");
}

/*
 * EFI 固件加载 PE/COFF 后进入这里，参数由 UEFI ABI 约定传入。
 *
 * image_handle 代表“当前这个已加载 EFI 镜像”。后面查询 Loaded Image Protocol、
 * 读取启动参数、调用 ExitBootServices 时都会用到它。
 *
 * 如果 efi_main 返回，返回值会交还给启动它的一方：U-Boot bootefi、UEFI Shell
 * 或固件 Boot Manager。真正的 OS loader 通常在 ExitBootServices 后跳进内核，
 * 成功路径不会再返回到这里。
 */
EFI_STATUS efi_main(EFI_HANDLE image_handle, efi_system_table_t *system_table)
{
    /* 当前阶段只探测固件信息，还没有使用 image_handle。 */
    (void)image_handle;

    puts(system_table, L"RVOS EFI boot\r\n");
    print_dtb_info(system_table);
    print_memory_map_info(system_table);

    return EFI_SUCCESS;
}

#include "efi_boot_info.h"

/* EFI configuration table 里用于标识 DTB 的标准 GUID。 */
static const efi_guid_t efi_dtb_table_guid = {
    0xb1b621d5, 0xf19c, 0x41a5,
    { 0x83, 0x0b, 0xd9, 0x15, 0x2c, 0x69, 0xaa, 0xe0 }
};

/* RISC-V UEFI Boot Protocol，用来按规范获取 boot hart id。 */
static const efi_guid_t riscv_efi_boot_protocol_guid = {
    0xccd15fec, 0x6f73, 0x4eec,
    { 0x83, 0x95, 0x3e, 0x69, 0xe4, 0xb9, 0x40, 0xbf }
};

typedef struct riscv_efi_boot_protocol riscv_efi_boot_protocol_t;

struct riscv_efi_boot_protocol {
    uint64_t revision;
    EFI_STATUS (*get_boot_hart_id)(
        riscv_efi_boot_protocol_t *this,
        UINTN *boot_hart_id
    );
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

static EFI_STATUS get_boot_hart_id(efi_system_table_t *st, uint64_t *boot_hart_id)
{
    riscv_efi_boot_protocol_t *boot_protocol = 0;
    EFI_STATUS status = st->boot_services->locate_protocol(
        (efi_guid_t *)&riscv_efi_boot_protocol_guid,
        0,
        (void **)&boot_protocol
    );

    if (status != EFI_SUCCESS) {
        return status;
    }

    UINTN hart_id = 0;
    status = boot_protocol->get_boot_hart_id(boot_protocol, &hart_id);
    if (status != EFI_SUCCESS) {
        return status;
    }

    *boot_hart_id = hart_id;
    return EFI_SUCCESS;
}

static void init_boot_info(struct kernel_boot_info *boot_info)
{
    boot_info->magic = KERNEL_BOOT_INFO_MAGIC;
    boot_info->version = KERNEL_BOOT_INFO_VERSION;
    boot_info->size = sizeof(*boot_info);
    boot_info->flags = 0;

    boot_info->dtb_phys = 0;
    boot_info->dtb_size = 0;

    boot_info->efi_memory_map_phys = 0;
    boot_info->efi_memory_map_size = 0;
    boot_info->efi_descriptor_size = 0;
    boot_info->efi_descriptor_version = 0;
    boot_info->reserved0 = 0;

    boot_info->boot_hart_id = 0;

    boot_info->kernel_phys_base = 0;
    boot_info->kernel_size = 0;

    boot_info->boot_info_phys = (uint64_t)(uintptr_t)boot_info;
    boot_info->boot_info_size = EFI_PAGE_SIZE;

    boot_info->kernel_stack_phys = 0;
    boot_info->kernel_stack_size = 0;
}

EFI_STATUS efi_collect_boot_info(
    EFI_HANDLE image_handle,
    efi_system_table_t *st,
    struct kernel_boot_info *boot_info,
    efi_memory_map_info_t *memory_map
)
{
    (void)image_handle;

    init_boot_info(boot_info);

    uint32_t *dtb = (uint32_t *)find_dtb(st);
    if (dtb) {
        boot_info->dtb_phys = (uint64_t)(uintptr_t)dtb;
        boot_info->dtb_size = be32_to_cpu(dtb[1]);
        boot_info->flags |= KERNEL_BOOT_HAS_DTB;
    }

    EFI_STATUS status = get_boot_hart_id(st, &boot_info->boot_hart_id);
    if (status != EFI_SUCCESS) {
        return status;
    }
    boot_info->flags |= KERNEL_BOOT_HAS_BOOT_HART_ID;

    status = efi_get_memory_map(st, memory_map);
    if (status != EFI_SUCCESS) {
        return status;
    }

    boot_info->efi_memory_map_phys = (uint64_t)(uintptr_t)memory_map->buffer;
    boot_info->efi_memory_map_size = memory_map->size;
    boot_info->efi_descriptor_size = memory_map->descriptor_size;
    boot_info->efi_descriptor_version = memory_map->descriptor_version;
    boot_info->flags |= KERNEL_BOOT_HAS_EFI_MEMORY_MAP;

    return EFI_SUCCESS;
}

void efi_print_boot_info(efi_system_table_t *st, const struct kernel_boot_info *boot_info)
{
    efi_puts(st, L"Boot info: magic=");
    efi_print_hex64(st, boot_info->magic);
    efi_puts(st, L", version=");
    efi_print_u64(st, boot_info->version);
    efi_puts(st, L", size=");
    efi_print_u64(st, boot_info->size);
    efi_puts(st, L", flags=");
    efi_print_hex64(st, boot_info->flags);
    efi_puts(st, L"\r\n");

    efi_puts(st, L"  boot_hart_id=");
    efi_print_u64(st, boot_info->boot_hart_id);
    efi_puts(st, L"\r\n");

    efi_puts(st, L"  dtb_phys=");
    efi_print_hex64(st, boot_info->dtb_phys);
    efi_puts(st, L", dtb_size=");
    efi_print_u64(st, boot_info->dtb_size);
    efi_puts(st, L"\r\n");

    efi_puts(st, L"  memory_map_phys=");
    efi_print_hex64(st, boot_info->efi_memory_map_phys);
    efi_puts(st, L", memory_map_size=");
    efi_print_u64(st, boot_info->efi_memory_map_size);
    efi_puts(st, L", desc_size=");
    efi_print_u64(st, boot_info->efi_descriptor_size);
    efi_puts(st, L", desc_version=");
    efi_print_u64(st, boot_info->efi_descriptor_version);
    efi_puts(st, L"\r\n");

    efi_puts(st, L"  boot_info_phys=");
    efi_print_hex64(st, boot_info->boot_info_phys);
    efi_puts(st, L", boot_info_size=");
    efi_print_u64(st, boot_info->boot_info_size);
    efi_puts(st, L"\r\n");

    efi_puts(st, L"  kernel_stack_phys=");
    efi_print_hex64(st, boot_info->kernel_stack_phys);
    efi_puts(st, L", kernel_stack_size=");
    efi_print_u64(st, boot_info->kernel_stack_size);
    efi_puts(st, L"\r\n");
}

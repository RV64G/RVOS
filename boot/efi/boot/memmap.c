#include "efi.h"

static UINTN pages_for_size(UINTN size)
{
    return (size + EFI_PAGE_SIZE - 1) / EFI_PAGE_SIZE;
}

EFI_STATUS efi_allocate_pages(
    efi_system_table_t *st,
    UINTN size,
    void **buffer,
    UINTN *pages
)
{
    uint64_t address = 0;

    *pages = pages_for_size(size);
    EFI_STATUS status = st->boot_services->allocate_pages(
        EFI_ALLOCATE_ANY_PAGES,
        EFI_LOADER_DATA,
        *pages,
        &address
    );

    if (status != EFI_SUCCESS) {
        *buffer = 0;
        *pages = 0;
        return status;
    }

    *buffer = (void *)(uintptr_t)address;
    return EFI_SUCCESS;
}

void efi_free_pages(efi_system_table_t *st, void *buffer, UINTN pages)
{
    if (buffer && pages != 0) {
        st->boot_services->free_pages((uint64_t)(uintptr_t)buffer, pages);
    }
}

static void print_memory_descriptor(
    efi_system_table_t *st,
    UINTN index,
    const efi_memory_descriptor_t *desc
)
{
    efi_puts(st, L"  [");
    efi_print_u64(st, index);
    efi_puts(st, L"] type=");
    efi_print_u64(st, desc->type);
    efi_puts(st, L", phys=");
    efi_print_hex64(st, desc->physical_start);
    efi_puts(st, L", pages=");
    efi_print_u64(st, desc->number_of_pages);
    efi_puts(st, L", attr=");
    efi_print_hex64(st, desc->attribute);
    efi_puts(st, L"\r\n");
}

/*
 * 分配 memory map buffer 本身会改变 memory map，所以读取必须允许重试。
 * 正式传给内核的 memory map 按页保存，内核接手后可以直接把这几页标为已占用。
 */
EFI_STATUS efi_get_memory_map(efi_system_table_t *st, efi_memory_map_info_t *info)
{
    UINTN query_size = 0;
    UINTN desc_size = 0;
    uint32_t desc_version = 0;

    EFI_STATUS status = st->boot_services->get_memory_map(
        &query_size, 0, &info->map_key, &desc_size, &desc_version
    );

    if (status != EFI_BUFFER_TOO_SMALL || desc_size == 0) {
        return status;
    }

    if (desc_size < sizeof(efi_memory_descriptor_t)) {
        return EFI_INVALID_PARAMETER;
    }

    info->buffer = 0;
    info->size = query_size;
    info->pages = 0;
    info->descriptor_size = desc_size;
    info->descriptor_version = desc_version;

    for (int retry = 0; retry < 8; retry++) {
        UINTN buffer_size = info->size + desc_size * 8;
        status = efi_allocate_pages(st, buffer_size, &info->buffer, &info->pages);
        if (status != EFI_SUCCESS) {
            return status;
        }

        info->size = info->pages * EFI_PAGE_SIZE;
        status = st->boot_services->get_memory_map(
            &info->size,
            info->buffer,
            &info->map_key,
            &info->descriptor_size,
            &info->descriptor_version
        );

        if (status == EFI_SUCCESS) {
            return EFI_SUCCESS;
        }

        efi_free_pages(st, info->buffer, info->pages);
        info->buffer = 0;
        info->pages = 0;

        if (status != EFI_BUFFER_TOO_SMALL) {
            return status;
        }
    }

    return EFI_BUFFER_TOO_SMALL;
}

void efi_free_memory_map(efi_system_table_t *st, efi_memory_map_info_t *info)
{
    efi_free_pages(st, info->buffer, info->pages);
    info->buffer = 0;
    info->size = 0;
    info->pages = 0;
}

void efi_print_memory_map(efi_system_table_t *st, const efi_memory_map_info_t *info)
{
    efi_puts(st, L"Memory map: ");
    efi_puts(st, L"size=");
    efi_print_u64(st, info->size);
    efi_puts(st, L", desc_size=");
    efi_print_u64(st, info->descriptor_size);
    efi_puts(st, L", entries=");
    efi_print_u64(st, info->size / info->descriptor_size);
    efi_puts(st, L", version=");
    efi_print_u64(st, info->descriptor_version);
    efi_puts(st, L", key=");
    efi_print_hex64(st, info->map_key);
    efi_puts(st, L"\r\n");

    UINTN entries = info->size / info->descriptor_size;
    for (UINTN i = 0; i < entries; i++) {
        uint8_t *entry = (uint8_t *)info->buffer + i * info->descriptor_size;
        print_memory_descriptor(st, i, (efi_memory_descriptor_t *)entry);
    }
}

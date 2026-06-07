#ifndef EFI_H
#define EFI_H

#include <stdint.h>

typedef uintptr_t UINTN;
typedef uint64_t EFI_STATUS;
/* EFI_HANDLE 是固件维护的不透明句柄，程序不能假设它的内部结构。 */
typedef void *EFI_HANDLE;
typedef unsigned short CHAR16;

#define EFI_SUCCESS 0
/* UEFI 错误码会把最高位置 1，低位才是具体错误编号。 */
#define EFI_ERROR(status) (0x8000000000000000ULL | (status))
#define EFI_INVALID_PARAMETER EFI_ERROR(2)
#define EFI_BUFFER_TOO_SMALL EFI_ERROR(5)

/* AllocatePool/AllocatePages 使用的内存类型。当前启动器自己的数据用 LoaderData。 */
#define EFI_LOADER_DATA 2
#define EFI_CONVENTIONAL_MEMORY 7
#define EFI_ALLOCATE_ANY_PAGES 0
#define EFI_ALLOCATE_ADDRESS 2
#define EFI_PAGE_SIZE 4096ULL

#define EFI_FILE_MODE_READ 0x0000000000000001ULL

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
typedef struct efi_system_table efi_system_table_t;
typedef struct efi_loaded_image_protocol efi_loaded_image_protocol_t;
typedef struct efi_simple_file_system_protocol efi_simple_file_system_protocol_t;
typedef struct efi_file_protocol efi_file_protocol_t;

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

struct efi_loaded_image_protocol {
    uint32_t revision;
    EFI_HANDLE parent_handle;
    efi_system_table_t *system_table;
    EFI_HANDLE device_handle;
    void *file_path;
    void *reserved;
    uint32_t load_options_size;
    void *load_options;
    void *image_base;
    uint64_t image_size;
    uint32_t image_code_type;
    uint32_t image_data_type;
    EFI_STATUS (*unload)(EFI_HANDLE image_handle);
};

struct efi_simple_file_system_protocol {
    uint64_t revision;
    EFI_STATUS (*open_volume)(
        efi_simple_file_system_protocol_t *this,
        efi_file_protocol_t **root
    );
};

struct efi_file_protocol {
    uint64_t revision;
    EFI_STATUS (*open)(
        efi_file_protocol_t *this,
        efi_file_protocol_t **new_handle,
        CHAR16 *file_name,
        uint64_t open_mode,
        uint64_t attributes
    );
    EFI_STATUS (*close)(efi_file_protocol_t *this);
    void *delete;
    EFI_STATUS (*read)(efi_file_protocol_t *this, UINTN *buffer_size, void *buffer);
    void *write;
    EFI_STATUS (*get_position)(efi_file_protocol_t *this, uint64_t *position);
    EFI_STATUS (*set_position)(efi_file_protocol_t *this, uint64_t position);
    void *get_info;
    void *set_info;
    void *flush;
};

/*
 * Boot Services 是退出 EFI 前能调用的服务表。这里按 UEFI 规范顺序声明到
 * GetMemoryMap 为止，后面的服务当前不用，暂时不展开。
 */
struct efi_boot_services {
    efi_table_header_t hdr;
    void *raise_tpl;
    void *restore_tpl;
    EFI_STATUS (*allocate_pages)(
        uint32_t type,
        uint32_t memory_type,
        UINTN pages,
        uint64_t *memory
    );
    EFI_STATUS (*free_pages)(uint64_t memory, UINTN pages);
    EFI_STATUS (*get_memory_map)(
        UINTN *memory_map_size,
        void *memory_map,
        UINTN *map_key,
        UINTN *descriptor_size,
        uint32_t *descriptor_version
    );
    EFI_STATUS (*allocate_pool)(
        uint32_t pool_type,
        UINTN size,
        void **buffer
    );
    EFI_STATUS (*free_pool)(void *buffer);
    void *create_event;
    void *set_timer;
    void *wait_for_event;
    void *signal_event;
    void *close_event;
    void *check_event;
    void *install_protocol_interface;
    void *reinstall_protocol_interface;
    void *uninstall_protocol_interface;
    EFI_STATUS (*handle_protocol)(
        EFI_HANDLE handle,
        efi_guid_t *protocol,
        void **interface
    );
    void *reserved;
    void *register_protocol_notify;
    void *locate_handle;
    void *locate_device_path;
    void *install_configuration_table;
    void *load_image;
    void *start_image;
    void *exit;
    void *unload_image;
    EFI_STATUS (*exit_boot_services)(EFI_HANDLE image_handle, UINTN map_key);
    void *get_next_monotonic_count;
    void *stall;
    void *set_watchdog_timer;
    void *connect_controller;
    void *disconnect_controller;
    void *open_protocol;
    void *close_protocol;
    void *open_protocol_information;
    void *protocols_per_handle;
    void *locate_handle_buffer;
    EFI_STATUS (*locate_protocol)(
        efi_guid_t *protocol,
        void *registration,
        void **interface
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
struct efi_system_table {
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
};

typedef struct {
    void *buffer;
    UINTN size;
    UINTN pages;
    UINTN map_key;
    UINTN descriptor_size;
    uint32_t descriptor_version;
} efi_memory_map_info_t;

EFI_STATUS efi_allocate_pages(
    efi_system_table_t *st,
    UINTN size,
    void **buffer,
    UINTN *pages
);
void efi_free_pages(efi_system_table_t *st, void *buffer, UINTN pages);

void efi_puts(efi_system_table_t *st, const CHAR16 *s);
void efi_print_hex64(efi_system_table_t *st, uint64_t value);
void efi_print_u64(efi_system_table_t *st, uint64_t value);
EFI_STATUS efi_get_memory_map(efi_system_table_t *st, efi_memory_map_info_t *info);
void efi_free_memory_map(efi_system_table_t *st, efi_memory_map_info_t *info);
void efi_print_memory_map(efi_system_table_t *st, const efi_memory_map_info_t *info);

#endif

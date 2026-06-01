#include "efi_boot_info.h"

/*
 * 这个文件只做 EFI loader 阶段的内核文件装载：
 *
 * 1. 通过 UEFI 文件协议打开 ESP 里的 \RVOS\KERNEL.ELF。
 * 2. 把整个 ELF 文件读到 EFI pool 临时缓冲区。
 * 3. 校验它确实是 RV64 little-endian executable。
 * 4. 按 program header 里的 PT_LOAD 段，把内核复制到 ELF 指定的物理地址。
 * 5. 把 ELF entry 返回给 main.c，后者退出 Boot Services 后再真正跳转。
 *
 * 它不是 RVOS 自己的文件系统，也不是运行期 ELF loader。这里用 EFI 的文件服务
 * 只是为了在内核接管硬件之前，从启动介质上拿到真正的 kernel.elf。
 *
 * 日志只保留阶段性摘要和错误：
 *
 * - 打不开、读不了、校验失败、申请内存失败时打印 EFI_STATUS。
 * - 校验成功时打印 entry、program header 信息和装载范围。
 * - 装载成功时打印物理起始地址和页数。
 *
 * 不给每个成功的 EFI 调用打日志。启动阶段输出太多会淹没真正的失败点，详细流程
 * 写在注释里，运行时只暴露“走到哪一步”和“哪里失败”。
 */

#define EI_NIDENT 16
#define ELFCLASS64 2
#define ELFDATA2LSB 1
#define ET_EXEC 2
#define EM_RISCV 243
#define EV_CURRENT 1
#define PT_LOAD 1
#define UINT64_MAX_VALUE (~0ULL)

static const efi_guid_t loaded_image_protocol_guid = {
    0x5b1b31a1, 0x9562, 0x11d2,
    { 0x8e, 0x3f, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b }
};

static const efi_guid_t simple_file_system_protocol_guid = {
    0x964e5b22, 0x6459, 0x11d2,
    { 0x8e, 0x39, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b }
};

struct elf64_ehdr {
    unsigned char e_ident[EI_NIDENT];
    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version;
    uint64_t e_entry;
    uint64_t e_phoff;
    uint64_t e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize;
    uint16_t e_phentsize;
    uint16_t e_phnum;
    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
};

/* ELF program header 描述“文件中的哪一段应该被装到内存哪里”。 */
struct elf64_phdr {
    uint32_t p_type;
    uint32_t p_flags;
    uint64_t p_offset;
    uint64_t p_vaddr;
    uint64_t p_paddr;
    uint64_t p_filesz;
    uint64_t p_memsz;
    uint64_t p_align;
};

static void print_status(
    efi_system_table_t *st,
    const CHAR16 *message,
    EFI_STATUS status
)
{
    efi_puts(st, message);
    efi_print_hex64(st, status);
    efi_puts(st, L"\r\n");
}

static uint64_t align_down(uint64_t value, uint64_t align)
{
    return value & ~(align - 1);
}

static uint64_t align_up(uint64_t value, uint64_t align)
{
    return (value + align - 1) & ~(align - 1);
}

static UINTN pages_for_range(uint64_t start, uint64_t end)
{
    return (UINTN)((end - start) / EFI_PAGE_SIZE);
}

static void byte_fill(void *buffer, UINTN size, unsigned char value)
{
    unsigned char *out = (unsigned char *)buffer;
    for (UINTN i = 0; i < size; i++) {
        out[i] = value;
    }
}

static void byte_copy(void *dst, const void *src, UINTN size)
{
    unsigned char *out = (unsigned char *)dst;
    const unsigned char *in = (const unsigned char *)src;
    for (UINTN i = 0; i < size; i++) {
        out[i] = in[i];
    }
}

/*
 * 固定约定内核文件放在 ESP 的 \RVOS\KERNEL.ELF。
 * 这里明确按目录逐级打开；如果固件或镜像布局不满足这个约定，启动阶段直接失败。
 *
 * root 是 open_volume() 返回的文件系统根目录句柄。这里先从 root 打开 RVOS 目录，
 * 再用 RVOS 目录句柄打开 KERNEL.ELF。打开文件后，RVOS 目录句柄就不再需要，必须
 * 立刻关闭；KERNEL.ELF 的文件句柄通过 file 参数交给调用者，后续读取完成后再关闭。
 */
static EFI_STATUS open_kernel_path(efi_file_protocol_t *root, efi_file_protocol_t **file)
{
    efi_file_protocol_t *rvos_dir = 0;
    EFI_STATUS status = root->open(
        root,
        &rvos_dir,
        L"RVOS",
        EFI_FILE_MODE_READ,
        0
    );
    if (status == EFI_SUCCESS) {
        status = rvos_dir->open(
            rvos_dir,
            file,
            L"KERNEL.ELF",
            EFI_FILE_MODE_READ,
            0
        );
        rvos_dir->close(rvos_dir);
        if (status == EFI_SUCCESS) {
            return EFI_SUCCESS;
        }
    }

    return status;
}

/*
 * 通过 image_handle 拿到 Loaded Image Protocol，再从 device_handle 打开当前 EFI app
 * 所在设备的文件系统。RVOS 要求 KERNEL.ELF 必须和 BOOTRISCV64.EFI 在同一启动介质。
 *
 * 调用链是：
 *
 * image_handle
 *   -> Loaded Image Protocol
 *   -> loaded_image->device_handle
 *   -> Simple File System Protocol
 *   -> open_volume() 得到根目录
 *   -> RVOS/KERNEL.ELF
 *
 * open_volume() 可以理解成“打开当前启动分区的根目录”。它返回的 root 不是文件内容，
 * 而是根目录句柄。open_kernel_path() 会从这个根目录继续打开 RVOS/KERNEL.ELF。
 *
 * 成功时返回的是 KERNEL.ELF 的文件句柄；root 目录句柄会在本函数内关闭。失败时
 * 不尝试扫描其它分区，避免误读另一块启动介质上的同名内核。
 */
static EFI_STATUS open_kernel_file(
    EFI_HANDLE image_handle,
    efi_system_table_t *st,
    efi_file_protocol_t **file
)
{
    efi_loaded_image_protocol_t *loaded_image = 0;
    EFI_STATUS status = st->boot_services->handle_protocol(
        image_handle,
        (efi_guid_t *)&loaded_image_protocol_guid,
        (void **)&loaded_image
    );
    if (status != EFI_SUCCESS) {
        return status;
    }

    efi_simple_file_system_protocol_t *file_system = 0;
    status = st->boot_services->handle_protocol(
        loaded_image->device_handle,
        (efi_guid_t *)&simple_file_system_protocol_guid,
        (void **)&file_system
    );
    if (status != EFI_SUCCESS) {
        return status;
    }

    efi_file_protocol_t *root = 0;
    status = file_system->open_volume(file_system, &root);
    if (status != EFI_SUCCESS) {
        return status;
    }

    status = open_kernel_path(root, file);
    root->close(root);

    return status;
}

/*
 * EFI 文件协议没有要求我们提前知道文件大小。这里先 seek 到末尾读当前位置，再回到
 * 开头一次性读取。这个 buffer 只是临时 I/O 缓冲，ELF 装载完成后会 free_pool。
 *
 * 后续如果 kernel ELF 变大，可以改成按段读取。但当前阶段一次性读完整文件能让
 * ELF 头、program header 和段内容都在同一个 buffer 里，校验逻辑更直接。
 */
static EFI_STATUS read_whole_file(
    efi_system_table_t *st,
    efi_file_protocol_t *file,
    void **buffer,
    UINTN *size
)
{
    uint64_t file_size = 0;
    EFI_STATUS status = file->set_position(file, UINT64_MAX);
    if (status != EFI_SUCCESS) {
        return status;
    }

    status = file->get_position(file, &file_size);
    if (status != EFI_SUCCESS) {
        return status;
    }

    status = file->set_position(file, 0);
    if (status != EFI_SUCCESS) {
        return status;
    }

    if (file_size == 0 || file_size > (uint64_t)(~(UINTN)0)) {
        return EFI_INVALID_PARAMETER;
    }

    status = st->boot_services->allocate_pool(EFI_LOADER_DATA, (UINTN)file_size, buffer);
    if (status != EFI_SUCCESS) {
        return status;
    }

    *size = (UINTN)file_size;
    UINTN read_size = *size;
    status = file->read(file, &read_size, *buffer);
    if (status != EFI_SUCCESS || read_size != *size) {
        st->boot_services->free_pool(*buffer);
        *buffer = 0;
        *size = 0;
        return status != EFI_SUCCESS ? status : EFI_INVALID_PARAMETER;
    }

    return EFI_SUCCESS;
}

/*
 * 校验 ELF 头，并计算所有 PT_LOAD 段覆盖的页对齐物理范围。
 *
 * section header 对运行装载没有意义；真正决定“内核怎么进内存”的是 program header。
 * 这里只接受 ET_EXEC，因为当前 kernel.lds 把内核链接到固定物理地址 0x80200000。
 * ELF 文件本身可能比运行时镜像大很多，因为文件里还会有 debug section、符号表、
 * 字符串表、section header 等工具数据。loader 不按文件大小申请内存，而是只看
 * PT_LOAD 段声明的 p_paddr/p_filesz/p_memsz。
 *
 * 这里不真正复制段，只做三件事：
 *
 * - 确认文件格式是当前 loader 能处理的 RV64 ELF。
 * - 确认 program header 表没有越过文件边界。
 * - 找出所有 PT_LOAD 段共同覆盖的物理页范围，交给 load_segments() 统一申请。
 */
static EFI_STATUS validate_kernel_elf(
    efi_system_table_t *st,
    const void *buffer,
    UINTN size,
    const struct elf64_ehdr **ehdr_out,
    const struct elf64_phdr **phdrs_out,
    uint64_t *load_start_out,
    uint64_t *load_end_out
)
{
    if (size < sizeof(struct elf64_ehdr)) {
        return EFI_INVALID_PARAMETER;
    }

    const struct elf64_ehdr *ehdr = (const struct elf64_ehdr *)buffer;
    if (ehdr->e_ident[0] != 0x7f ||
        ehdr->e_ident[1] != 'E' ||
        ehdr->e_ident[2] != 'L' ||
        ehdr->e_ident[3] != 'F' ||
        ehdr->e_ident[4] != ELFCLASS64 ||
        ehdr->e_ident[5] != ELFDATA2LSB ||
        ehdr->e_type != ET_EXEC ||
        ehdr->e_machine != EM_RISCV ||
        ehdr->e_version != EV_CURRENT ||
        ehdr->e_ehsize != sizeof(struct elf64_ehdr) ||
        ehdr->e_phentsize != sizeof(struct elf64_phdr) ||
        ehdr->e_phnum == 0) {
        return EFI_INVALID_PARAMETER;
    }

    uint64_t phdr_size = (uint64_t)ehdr->e_phentsize * ehdr->e_phnum;
    if (ehdr->e_phoff > size || phdr_size > size - ehdr->e_phoff) {
        return EFI_INVALID_PARAMETER;
    }

    const struct elf64_phdr *phdrs =
        (const struct elf64_phdr *)((const uint8_t *)buffer + ehdr->e_phoff);
    uint64_t load_segments = 0;
    uint64_t load_start = UINT64_MAX_VALUE;
    uint64_t load_end = 0;
    for (uint16_t i = 0; i < ehdr->e_phnum; i++) {
        const struct elf64_phdr *phdr = &phdrs[i];
        if (phdr->p_type != PT_LOAD) {
            continue;
        }

        if (phdr->p_memsz < phdr->p_filesz ||
            phdr->p_offset > size ||
            phdr->p_filesz > size - phdr->p_offset ||
            phdr->p_paddr > UINT64_MAX_VALUE - phdr->p_memsz) {
            return EFI_INVALID_PARAMETER;
        }

        uint64_t segment_start = align_down(phdr->p_paddr, EFI_PAGE_SIZE);
        uint64_t segment_end = align_up(phdr->p_paddr + phdr->p_memsz, EFI_PAGE_SIZE);
        if (segment_end <= segment_start) {
            return EFI_INVALID_PARAMETER;
        }

        if (segment_start < load_start) {
            load_start = segment_start;
        }
        if (segment_end > load_end) {
            load_end = segment_end;
        }
        load_segments++;
    }

    if (load_segments == 0 ||
        ehdr->e_entry < load_start ||
        ehdr->e_entry >= load_end) {
        return EFI_INVALID_PARAMETER;
    }

    efi_puts(st, L"Kernel ELF: found\r\n");
    efi_puts(st, L"  entry=");
    efi_print_hex64(st, ehdr->e_entry);
    efi_puts(st, L", phoff=");
    efi_print_u64(st, ehdr->e_phoff);
    efi_puts(st, L", phnum=");
    efi_print_u64(st, ehdr->e_phnum);
    efi_puts(st, L", load=");
    efi_print_u64(st, load_segments);
    efi_puts(st, L", range=");
    efi_print_hex64(st, load_start);
    efi_puts(st, L"..");
    efi_print_hex64(st, load_end);
    efi_puts(st, L"\r\n");

    *ehdr_out = ehdr;
    *phdrs_out = phdrs;
    *load_start_out = load_start;
    *load_end_out = load_end;
    return EFI_SUCCESS;
}

/*
 * 为所有 PT_LOAD 段一次性申请连续物理页，再逐段复制文件内容。
 *
 * 不能简单地“每个 PT_LOAD 申请一次页”：两个段可能落在同一页内，逐段申请会互相
 * 冲突。先算总范围再统一 AllocateAddress，能保证这片物理地址归 loader/内核所有。
 * 例如当前最小内核里 .text 和 .rodata 是两个 PT_LOAD，但它们都落在
 * 0x80200000..0x80200fff 这一页内。如果按段分别申请，第二个段会再次申请同一页。
 *
 * 先清零整段内存，是为了覆盖 .bss 这类 p_memsz > p_filesz 的区域。
 *
 * EFI_ALLOCATE_ADDRESS 表示“我要的就是 ELF 声明的物理地址”，不是让固件随便找一块。
 * 如果这段地址已经被固件占用，启动应当失败，而不是偷偷换地址；当前内核还不是
 * 可重定位内核。
 */
static EFI_STATUS load_segments(
    efi_system_table_t *st,
    const void *buffer,
    const struct elf64_ehdr *ehdr,
    const struct elf64_phdr *phdrs,
    uint64_t load_start,
    uint64_t load_end
)
{
    uint64_t load_address = load_start;
    UINTN pages = pages_for_range(load_start, load_end);
    EFI_STATUS status = st->boot_services->allocate_pages(
        EFI_ALLOCATE_ADDRESS,
        EFI_LOADER_DATA,
        pages,
        &load_address
    );
    if (status != EFI_SUCCESS) {
        print_status(st, L"Kernel memory allocation failed, status=", status);
        return status;
    }

    byte_fill((void *)(uintptr_t)load_start, pages * EFI_PAGE_SIZE, 0);

    for (uint16_t i = 0; i < ehdr->e_phnum; i++) {
        const struct elf64_phdr *phdr = &phdrs[i];
        if (phdr->p_type != PT_LOAD || phdr->p_filesz == 0) {
            continue;
        }

        byte_copy(
            (void *)(uintptr_t)phdr->p_paddr,
            (const uint8_t *)buffer + phdr->p_offset,
            (UINTN)phdr->p_filesz
        );
    }

    efi_puts(st, L"Kernel ELF: loaded at ");
    efi_print_hex64(st, load_start);
    efi_puts(st, L", pages=");
    efi_print_u64(st, pages);
    efi_puts(st, L"\r\n");

    return EFI_SUCCESS;
}

/*
 * main.c 调用的公开入口。成功返回时：
 *
 * - kernel ELF 的 PT_LOAD 段已经位于它声明的物理地址；
 * - *entry 是 ELF header 里的入口地址；
 * - 临时文件 buffer 已释放；
 * - 已装载的内核页不会释放，后续由 RVOS 根据 boot_info/memory map 接管。
 *
 * 完整流程：
 *
 * open_kernel_file()
 *   -> read_whole_file()
 *   -> close(KERNEL.ELF)
 *   -> validate_kernel_elf()
 *   -> load_segments()
 *   -> *entry = ehdr->e_entry
 *   -> free_pool(临时 ELF buffer)
 *
 * 这里不调用 ExitBootServices，也不直接跳转。退出 EFI、切栈、清 satp 和跳 entry
 * 由 main.c 统一处理。
 */
EFI_STATUS efi_load_kernel_elf(
    EFI_HANDLE image_handle,
    efi_system_table_t *st,
    uint64_t *entry
)
{
    efi_file_protocol_t *file = 0;
    EFI_STATUS status = open_kernel_file(image_handle, st, &file);
    if (status != EFI_SUCCESS) {
        print_status(st, L"Kernel ELF open failed, status=", status);
        return status;
    }

    void *buffer = 0;
    UINTN size = 0;
    status = read_whole_file(st, file, &buffer, &size);
    file->close(file);
    if (status != EFI_SUCCESS) {
        print_status(st, L"Kernel ELF read failed, status=", status);
        return status;
    }

    const struct elf64_ehdr *ehdr = 0;
    const struct elf64_phdr *phdrs = 0;
    uint64_t load_start = 0;
    uint64_t load_end = 0;
    status = validate_kernel_elf(
        st,
        buffer,
        size,
        &ehdr,
        &phdrs,
        &load_start,
        &load_end
    );
    if (status == EFI_SUCCESS) {
        status = load_segments(st, buffer, ehdr, phdrs, load_start, load_end);
        if (status == EFI_SUCCESS) {
            *entry = ehdr->e_entry;
        }
    }

    st->boot_services->free_pool(buffer);
    if (status != EFI_SUCCESS) {
        print_status(st, L"Kernel ELF load failed, status=", status);
    }

    return status;
}

#include "ramfs.h"

#include "printk.h"
#include "string.h"

#define RAMFS_MAGIC_SIZE 8
#define RAMFS_VERSION 1

struct ramfs_header
{
    /* "RVOSRAM\0"，用于确认这段内存确实是我们自己的 initramfs 镜像。 */
    unsigned char magic[RAMFS_MAGIC_SIZE];
    uint32_t version;
    /* header_size 让后续格式升级时仍能明确文件表从哪里开始。 */
    uint32_t header_size;
    uint32_t file_count;
    uint32_t reserved;
};

struct ramfs_entry
{
    /*
     * path 和 data 都用“相对镜像起点的 offset + size”描述。这样镜像被链接到
     * 任意物理地址后仍然可读，不需要在构建时写死指针。
     */
    uint32_t path_offset;
    uint32_t path_size;
    uint64_t data_offset;
    uint64_t data_size;
    uint32_t flags;
    uint32_t reserved;
};

static const unsigned char ramfs_magic[RAMFS_MAGIC_SIZE] = {
    'R', 'V', 'O', 'S', 'R', 'A', 'M', '\0'
};

static const unsigned char *ramfs_image;
static uint64_t ramfs_image_size;
static const struct ramfs_entry *ramfs_entries;
static uint32_t ramfs_file_count;

static int range_fits(uint64_t offset, uint64_t size, uint64_t total)
{
    return offset <= total && size <= total - offset;
}

static int path_matches(const char *path, const unsigned char *stored,
                        uint32_t stored_size)
{
    /*
     * 镜像里的 path 不是 NUL 结尾字符串，而是定长字节串。先比较长度，再比较内容，
     * 可以避免读取越过 path_size。
     */
    uint64_t path_len = strlen(path);
    if (path_len != stored_size)
    {
        return 0;
    }

    return memcmp(path, stored, stored_size) == 0;
}

int ramfs_init(const void *image, uint64_t image_size)
{
    const struct ramfs_header *header = (const struct ramfs_header *)image;
    uint64_t table_size;

    if (!image || image_size < sizeof(*header))
    {
        printk("Initramfs rejected: header missing\r\n");
        return 0;
    }

    if (memcmp(header->magic, ramfs_magic, RAMFS_MAGIC_SIZE) != 0 ||
        header->version != RAMFS_VERSION ||
        header->header_size != sizeof(*header))
    {
        printk("Initramfs rejected: bad header\r\n");
        return 0;
    }

    /*
     * 初始化阶段只校验文件表本身在镜像内。每个文件的数据范围在 lookup 时再校验，
     * 这样启动期不用遍历所有文件，也能在命中文件前发现损坏表项。
     */
    table_size = (uint64_t)header->file_count * sizeof(struct ramfs_entry);
    if (!range_fits(header->header_size, table_size, image_size))
    {
        printk("Initramfs rejected: file table outside image\r\n");
        return 0;
    }

    ramfs_image = (const unsigned char *)image;
    ramfs_image_size = image_size;
    ramfs_entries =
        (const struct ramfs_entry *)(const void *)(ramfs_image +
            header->header_size);
    ramfs_file_count = header->file_count;

    printk("Initramfs ready\r\n");
    printk_dec_field("files", ramfs_file_count);
    return 1;
}

int ramfs_lookup(const char *path, struct ramfs_file *file)
{
    if (!path || !file || !ramfs_image)
    {
        return 0;
    }

    for (uint32_t i = 0; i < ramfs_file_count; i++)
    {
        const struct ramfs_entry *entry = &ramfs_entries[i];
        const unsigned char *stored_path;

        /*
         * 即使 initramfs 是构建系统生成的，内核也不能直接信任 offset。这里保证后续
         * stored_path 和 file->data 都不会指到镜像外面。
         */
        if (!range_fits(entry->path_offset, entry->path_size,
                        ramfs_image_size) ||
            !range_fits(entry->data_offset, entry->data_size,
                        ramfs_image_size))
        {
            return 0;
        }

        stored_path = ramfs_image + entry->path_offset;
        if (!path_matches(path, stored_path, entry->path_size))
        {
            continue;
        }

        file->data = ramfs_image + entry->data_offset;
        file->size = entry->data_size;
        file->flags = entry->flags;
        return 1;
    }

    return 0;
}

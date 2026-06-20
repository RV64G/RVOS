#ifndef KERNEL_RAMFS_H
#define KERNEL_RAMFS_H

#include <stdint.h>

struct ramfs_file
{
    /* 指向 initramfs 镜像内部的只读文件内容，不会复制出一份新缓冲区。 */
    const void *data;
    uint64_t size;
    uint32_t flags;
};

/*
 * 初始化内核启动期只读文件包。image 必须在当前页表中可读，且在 ramfs 使用期间
 * 一直有效；当前它来自 kernel ELF 的 .initramfs section。
 */
int ramfs_init(const void *image, uint64_t image_size);

/*
 * 按绝对路径查找文件。当前 ramfs 不解析目录层级，只把路径当成普通字符串匹配，
 * 例如 "/bin/hello"。
 */
int ramfs_lookup(const char *path, struct ramfs_file *file);

#endif

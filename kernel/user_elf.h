#ifndef KERNEL_USER_ELF_H
#define KERNEL_USER_ELF_H

#include <stdint.h>

#include "vm.h"

/**
 * 把一个 ELF64 用户程序加载进指定用户地址空间。
 *
 * 当前 loader 只支持 RV64 little-endian ET_EXEC 和 PT_LOAD。它会为所有 PT_LOAD
 * 覆盖的虚拟地址范围分配物理页、建立 U-mode 映射，并返回 ELF entry。
 */
int user_elf_load(struct vm_space *space, const void *elf, uint64_t elf_size,
                  uint64_t *entry);

#endif

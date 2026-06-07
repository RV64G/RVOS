#ifndef MM_EARLY_VM_H
#define MM_EARLY_VM_H

#include "kernel_boot_info.h"
#include "vm.h"

/**
 * 建立并启用第一张内核 Sv39 页表。
 *
 * @param boot_info EFI loader 传入的启动 ABI。
 * @return 成功返回 1，失败返回 0。
 *
 * 这个函数只负责启动期必须立即可访问的恒等映射。真正的页表操作在 vm.c 中。
 */
int early_vm_enable(const struct kernel_boot_info *boot_info);

/**
 * 返回当前内核页表对象。
 *
 * 后续 MMIO、vmalloc、用户地址空间共享内核映射等功能可以基于这张 kernel_vm 继续
 * 扩展。
 */
struct vm_space *kernel_vm_space(void);

#endif

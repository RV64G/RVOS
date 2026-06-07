#ifndef KERNEL_DTB_H
#define KERNEL_DTB_H

#include "kernel_boot_info.h"

int dtb_init(const struct kernel_boot_info *boot_info);

#endif

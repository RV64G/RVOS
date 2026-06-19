    .section .text
    .globl jump_to_kernel
    .type jump_to_kernel, @function

/*
 * void jump_to_kernel(entry, stack_top, boot_info)
 *
 * a0 = kernel entry
 * a1 = stack top
 * a2 = boot_info
 *
 * 进入内核前切到内核栈，并用 jump 而不是 call 交出控制权。
 *
 * 不在 EFI 镜像里清 satp：有些固件的 EFI image 运行地址依赖当前页表。如果在这里
 * 关闭分页，CPU 会继续按物理地址解释后面的 sfence/jump 指令，LPi3A 这类固件会在
 * EFI 镜像内触发取指/访存异常。进入 kernel 后 PC 已经落在内核镜像地址，再由内核
 * 自己关闭旧页表并接管地址空间。
 *
 * 内核不应该返回 EFI loader。
 */
jump_to_kernel:
    mv sp, a1
    mv t0, a0
    mv a0, a2
    jr t0

    .size jump_to_kernel, . - jump_to_kernel

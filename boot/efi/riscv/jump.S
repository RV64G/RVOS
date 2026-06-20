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
 * 退出 Boot Services 后，固件可能仍留下自己的页表，但这张页表不属于内核，也不保证
 * 映射 kernel ELF 的物理入口。这里先清 satp 回到物理地址直通环境，再跳到内核。
 * 内核入口会再清一次 satp，作为从其它启动路径进入时的兜底。
 *
 * 内核不应该返回 EFI loader。
 */
jump_to_kernel:
    mv sp, a1
    mv t0, a0
    mv a0, a2
    csrw satp, zero
    sfence.vma
    jr t0

    .size jump_to_kernel, . - jump_to_kernel

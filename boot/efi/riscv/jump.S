    .section .text
    .globl rvos_jump_to_kernel
    .type rvos_jump_to_kernel, @function

/*
 * void rvos_jump_to_kernel(entry, stack_top, boot_info)
 *
 * a0 = kernel entry
 * a1 = stack top
 * a2 = boot_info
 *
 * 进入内核前切到内核栈，并用 jump 而不是 call 交出控制权。
 * UEFI 固件可能还开着自己的 S-mode 页表；内核入口当前按物理地址链接，
 * 所以交接前先清 satp，并刷新地址转换缓存。内核不应该返回 EFI loader。
 */
rvos_jump_to_kernel:
    mv sp, a1
    mv t0, a0
    mv a0, a2
    csrw satp, zero
    sfence.vma
    jr t0

    .size rvos_jump_to_kernel, . - rvos_jump_to_kernel

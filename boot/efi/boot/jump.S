    .section .text
    .globl rvos_jump_to_kernel_stub
    .type rvos_jump_to_kernel_stub, @function

/*
 * void rvos_jump_to_kernel_stub(stack_top, boot_info)
 *
 * a0 = stack top
 * a1 = boot_info
 *
 * 进入内核前切到内核栈，并用 jump 而不是 call 交出控制权。
 * 内核不应该返回 EFI loader。
 */
rvos_jump_to_kernel_stub:
    mv sp, a0
    mv a0, a1
    lla t0, rvos_kernel_stub
    jr t0

    .size rvos_jump_to_kernel_stub, . - rvos_jump_to_kernel_stub

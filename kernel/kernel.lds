OUTPUT_ARCH(riscv)
ENTRY(kernel_entry)

SECTIONS
{
    /*
     * 当前内核仍是 ET_EXEC 固定物理地址镜像。默认地址沿用 QEMU virt /
     * VisionFive2/Mars 上常见的 0x80200000；如果某块板子的 EFI memory map 不允许
     * 分配这段地址，可以在构建时覆盖：
     *
     *   make efi-esp KERNEL_PHYS_BASE=0x12000000
     *
     * TODO: 后续改成可重定位内核：EFI loader 从 memory map 选择可用物理地址，
     * kernel 通过 boot_info 接收实际加载位置，再建立固定虚拟地址映射。
     */
    . = @KERNEL_PHYS_BASE@;

    __kernel_start = .;
    __kernel_text_start = .;
    .text : {
        *(.text .text.*)
    }
    __kernel_text_end = .;

    . = ALIGN(0x1000);
    __kernel_rodata_start = .;
    .rodata : {
        *(.rodata .rodata.*)
    }
    __kernel_rodata_end = .;

    . = ALIGN(0x1000);
    __user_start = .;
    .user : {
        KEEP(*(.user .user.*))
    }
    . = ALIGN(0x1000);
    __user_end = .;

    . = ALIGN(0x1000);
    __kernel_data_start = .;
    .data : {
        *(.sdata .sdata.*)
        *(.data .data.*)
    }
    __kernel_data_end = .;

    . = ALIGN(0x1000);
    __kernel_bss_start = .;
    .bss : {
        *(.sbss .sbss.*)
        *(.bss .bss.*)
        *(COMMON)
    }
    __kernel_bss_end = .;

    . = ALIGN(0x1000);
    __kernel_end = .;
}

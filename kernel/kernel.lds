OUTPUT_ARCH(riscv)
ENTRY(kernel_entry)

SECTIONS
{
    . = 0x80200000;

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

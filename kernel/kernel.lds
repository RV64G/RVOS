OUTPUT_ARCH(riscv)
ENTRY(rvos_kernel_entry)

SECTIONS
{
    . = 0x80200000;

    .text : {
        *(.text .text.*)
    }

    .rodata : {
        *(.rodata .rodata.*)
    }

    .data : {
        *(.sdata .sdata.*)
        *(.data .data.*)
    }

    .bss : {
        *(.sbss .sbss.*)
        *(.bss .bss.*)
        *(COMMON)
    }
}

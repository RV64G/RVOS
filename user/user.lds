OUTPUT_ARCH(riscv)
ENTRY(_start)

SECTIONS
{
    . = 0x0000000000010000;

    .text : {
        *(.text .text.*)
    }

    . = ALIGN(0x1000);
    .rodata : {
        *(.rodata .rodata.*)
    }

    . = ALIGN(0x1000);
    .data : {
        *(.sdata .sdata.*)
        *(.data .data.*)
    }

    . = ALIGN(0x1000);
    .bss : {
        *(.sbss .sbss.*)
        *(.bss .bss.*)
        *(COMMON)
    }
}

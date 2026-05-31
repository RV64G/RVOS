OUTPUT_FORMAT("elf64-littleriscv")
ENTRY(efi_main)

SECTIONS
{
    . = 0x1000;

    .text : {
        *(.text .text.*)
    }

    . = ALIGN(0x1000);
    .rodata : {
        *(.srodata .srodata.*)
        *(.rodata .rodata.*)
    }

    . = ALIGN(0x1000);
    .data : {
        *(.sdata .sdata.*)
        *(.data .data.*)
        *(.dynamic)
    }

    . = ALIGN(0x1000);
    .reloc : {
        KEEP(*(.reloc))
    }

    /DISCARD/ : {
        *(.comment)
        *(.eh_frame*)
        *(.note*)
        *(.riscv.attributes)
        *(.debug*)
    }
}

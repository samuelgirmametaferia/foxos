ENTRY(kernel_entry)
SECTIONS
{
    . = 0x10000;        /* Load + link address in memory (matches bootloader) */
    .text : { *(.text*) }
    .rodata : { *(.rodata*) }
    .data : { *(.data*) }
    .bss : {
        __bss_start = .;
        *(.bss*) *(COMMON)
        __bss_end = .;
    }
}

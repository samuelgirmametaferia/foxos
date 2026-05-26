ENTRY(kernel_entry)
SECTIONS
{
    . = 0x200000;
    .text : { *(.text*) }
    .rodata : { *(.rodata*) }
    .data : { *(.data*) }
    .bss : {
        __bss_start = .;
        *(.bss*)
        *(.stack*)
        *(COMMON)
        __bss_end = .;
    }
}

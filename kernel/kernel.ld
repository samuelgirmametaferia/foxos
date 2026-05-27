ENTRY(kernel_entry)
SECTIONS
{
    . = 0x200000;
    .text : {
        *(.text*)
    }
    . = ALIGN(0x1000);
    .rodata : {
        *(.rodata*)
        *(.eh_frame*)
    }
    . = ALIGN(0x1000);
    .data : {
        *(.got*)
        *(.data*)
    }
    . = ALIGN(0x1000);
    .bss : {
        __bss_start = .;
        *(.bss*)
        *(.stack*)
        *(COMMON)
        __bss_end = .;
    }
}

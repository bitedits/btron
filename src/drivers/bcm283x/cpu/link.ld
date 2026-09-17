ENTRY(_start)

SECTIONS
{
    . = 0x80000;
    __stack_top = .;

    .text : {
        KEEP(*(.text._start))
        *(.text*)
    }
    .rodata : {
        *(.rodata*)
    }
    .data : {
        *(.data*)
    }
    .bss : {
        . = ALIGN(8);
        __bss_start = .;
        *(.bss*)
        *(COMMON)
        . = ALIGN(8);
        __bss_end = .;
    }
    _end = .;
}

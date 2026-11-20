/* Bare-metal RP2040 image: the simulation starts at 0x10000000 (no boot2). */
MEMORY
{
    FLASH (rx)  : ORIGIN = 0x10000000, LENGTH = 2M
    RAM   (rwx) : ORIGIN = 0x20000000, LENGTH = 256K
}
ENTRY(_start)
SECTIONS
{
    .text : { KEEP(*(.entry)) *(.text*) *(.rodata*) } > FLASH
    .data : { _data_start = .; *(.data*) _data_end = .; } > RAM AT > FLASH
    _data_load = LOADADDR(.data);
    .bss (NOLOAD) : { _bss_start = .; *(.bss*) *(COMMON) _bss_end = .; } > RAM
    _stack_top = ORIGIN(RAM) + LENGTH(RAM);
}

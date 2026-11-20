/* Bare-metal RP2350 image: entry (RISC-V) or vector table (Arm) at the start
   of the flash, then the IMAGE_DEF block, within the first 4 kB as the
   bootrom requires. */
MEMORY
{
    FLASH (rx)  : ORIGIN = 0x10000000, LENGTH = 4M
    RAM   (rwx) : ORIGIN = 0x20000000, LENGTH = 520K
}
ENTRY(_start)
SECTIONS
{
    .text : { KEEP(*(.entry)) KEEP(*(.image_def)) *(.text*) *(.rodata*) *(.srodata*) } > FLASH
    .data : { _data_start = .; *(.data*) *(.sdata*) _data_end = .; } > RAM AT > FLASH
    _data_load = LOADADDR(.data);
    .bss (NOLOAD) : { _bss_start = .; *(.bss*) *(.sbss*) *(COMMON) _bss_end = .; } > RAM
    _stack_top = ORIGIN(RAM) + LENGTH(RAM);
}

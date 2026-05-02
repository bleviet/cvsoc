/*
 * linker.ld — Linker script for DDR3 FPGA+HPS shared access test.
 *
 * Application runs from DDR3 at 0x00100000.
 * DDR3 controller must be initialised by U-Boot SPL first.
 *
 * Memory layout:
 *   0x00100000 – 0x001FFFFF  Application (code + data + stack, 1 MB)
 *   0x00200000 – 0x02FFFFFF  HPS test region   (region A, 46 MB)
 *   0x03000000 – 0x03FFFFFF  FPGA test region   (region B, 16 MB)
 */

ENTRY(_start)

MEMORY
{
    DDR3_APP (rwx) : ORIGIN = 0x00100000, LENGTH = 1M
}

SECTIONS
{
    .startup : {
        *(.startup)
    } > DDR3_APP

    .text : {
        *(.text)
        *(.text.*)
        *(.rodata)
        *(.rodata.*)
    } > DDR3_APP

    .data : {
        *(.data)
        *(.data.*)
    } > DDR3_APP

    .bss : {
        __bss_start = .;
        *(.bss)
        *(.bss.*)
        *(COMMON)
        __bss_end = .;
    } > DDR3_APP

    _stack_top = ORIGIN(DDR3_APP) + LENGTH(DDR3_APP) - 4;
}

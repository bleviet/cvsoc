/*
 * linker.ld — Linker script for bare-metal ARM DDR3 memory test application.
 *
 * This application runs from DDR3 SDRAM.  The DDR3 controller must be
 * initialised by U-Boot SPL before the application is loaded.
 *
 * DDR3 address map (from the ARM perspective):
 *   0x00000000 – 0x000FFFFF  Reserved (SPL, U-Boot workspace)
 *   0x00100000 – 0x001FFFFF  Application code + data + stack  (1 MB)
 *   0x00200000 – 0x03FFFFFF  Test region  (62 MB, used by the DDR3 test)
 *
 * Section layout:
 *   .startup  — reset vector and startup code (must be first)
 *   .text     — application code and read-only data
 *   .data     — initialised global/static variables
 *   .bss      — zero-initialised global/static variables
 *   [stack]   — grows downward from 0x001FFFFC
 */

ENTRY(_start)

MEMORY
{
    /* Application region: 1 MB at 0x00100000 */
    DDR3_APP (rwx) : ORIGIN = 0x00100000, LENGTH = 1M
}

SECTIONS
{
    /* Startup code — placed first so _start lands at 0x00100000 */
    .startup : {
        *(.startup)
    } > DDR3_APP

    /* Code and read-only data */
    .text : {
        *(.text)
        *(.text.*)
        *(.rodata)
        *(.rodata.*)
    } > DDR3_APP

    /* Initialised data */
    .data : {
        *(.data)
        *(.data.*)
    } > DDR3_APP

    /* Zero-initialised data */
    .bss : {
        __bss_start = .;
        *(.bss)
        *(.bss.*)
        *(COMMON)
        __bss_end = .;
    } > DDR3_APP

    /* Stack top — end of the 1 MB application region (-4 for alignment) */
    _stack_top = ORIGIN(DDR3_APP) + LENGTH(DDR3_APP) - 4;
}

/*
 * linker.ld — Linker script for bare-metal ARM application running from
 * HPS On-Chip RAM (OCRAM) on the Cyclone V SoC (DE10-Nano).
 *
 * OCRAM address map (from the Cyclone V HPS TRM):
 *   Base : 0xFFFF0000
 *   Size : 64 KiB
 *   Top  : 0xFFFFFFFF
 *
 * Section layout within OCRAM:
 *   .startup  — reset vector and startup code (must be first)
 *   .text     — application code and read-only data
 *   .data     — initialised global/static variables
 *   .bss      — zero-initialised global/static variables
 *   [stack]   — grows downward from _stack_top (top of OCRAM)
 */

ENTRY(_start)

MEMORY
{
    OCRAM (rwx) : ORIGIN = 0xFFFF0000, LENGTH = 64K
}

SECTIONS
{
    /* Startup code — placed first so _start lands at the OCRAM base address */
    .startup : {
        *(.startup)
    } > OCRAM

    /* Code and read-only data */
    .text : {
        *(.text)
        *(.text.*)
        *(.rodata)
        *(.rodata.*)
    } > OCRAM

    /* Initialised data */
    .data : {
        *(.data)
        *(.data.*)
    } > OCRAM

    /* Zero-initialised data — startup.S clears this region before main() */
    .bss : {
        __bss_start = .;
        *(.bss)
        *(.bss.*)
        *(COMMON)
        __bss_end = .;
    } > OCRAM

    /* Stack top — immediately past the end of OCRAM */
    _stack_top = ORIGIN(OCRAM) + LENGTH(OCRAM);
}

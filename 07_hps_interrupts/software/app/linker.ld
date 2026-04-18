/*
 * linker.ld — Linker script for the bare-metal HPS interrupt demo.
 *
 * Extends 05_hps_led/software/app/linker.ld to define two stack regions
 * inside the 64 KB HPS On-Chip RAM (OCRAM):
 *
 *   _irq_stack_top  Top of OCRAM − 4   (IRQ mode stack — 512 B reserved)
 *   _svc_stack_top  _irq_stack_top − 512  (SVC mode stack — rest of OCRAM)
 *
 * OCRAM layout (high addresses at top):
 *
 *   0xFFFFFFFF ┐
 *              │  IRQ mode stack (grows ↓, 512 B headroom)
 *   0xFFFFDFFF ├─ _irq_stack_top = 0xFFFFFFFC
 *   0xFFFFDFFB │  ← _svc_stack_top = 0xFFFFFDFC
 *              │  SVC mode stack (grows ↓)
 *              │
 *              │  .bss / .data / .text / .startup
 *   0xFFFF0000 ┘
 *
 * Both stacks grow downward (full-descending, as required by the ARM ABI).
 */

ENTRY(_start)

MEMORY
{
    OCRAM (rwx) : ORIGIN = 0xFFFF0000, LENGTH = 64K
}

SECTIONS
{
    /* Startup code — placed first so _start lands at OCRAM base address */
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

    /*
     * Stack symbols — NOT sections; just address constants for startup.S.
     * 512 bytes (0x200) are reserved for the IRQ mode stack at the top.
     * The SVC mode stack uses all remaining space below that.
     */
    _irq_stack_top = ORIGIN(OCRAM) + LENGTH(OCRAM) - 4;
    _svc_stack_top = _irq_stack_top - 0x200;
}

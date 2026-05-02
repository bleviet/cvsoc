/*
 * startup.S — Minimal ARM Cortex-A9 startup for bare-metal DDR3 test.
 *
 * This variant runs from DDR3 SDRAM at 0x00100000 (1 MB offset), which
 * requires the U-Boot SPL to have already initialised the DDR3 controller.
 *
 * Execution begins at _start immediately after OpenOCD or U-Boot transfers
 * control.  This stub:
 *   1. Provides an ARM exception vector table at offset 0.
 *   2. Switches to SVC (Supervisor) mode with IRQ and FIQ disabled.
 *   3. Sets the stack pointer to 0x001FFFFC (end of 2 MB region).
 *   4. Zero-initialises the .bss section.
 *   5. Calls main().
 *   6. Loops forever if main() returns.
 *
 * Memory layout in DDR3:
 *   0x00100000  code + rodata + data  (up to ~1 MB)
 *   0x001FFFFC  stack top             (grows downward)
 *   0x00200000+ test region           (untouched by the application itself)
 */

    .global _start
    .section .startup, "ax", %progbits

/* ------------------------------------------------------------------
 * ARM Exception Vector Table (must be at offset 0 of the binary).
 * Each entry is a branch to _start, causing a full re-initialisation.
 * ------------------------------------------------------------------ */
_vectors:
    b       _start          /* 0x00: Reset */
    b       _start          /* 0x04: Undefined Instruction */
    b       _start          /* 0x08: Software Interrupt (SVC) */
    b       _start          /* 0x0C: Prefetch Abort */
    b       _start          /* 0x10: Data Abort */
    b       _start          /* 0x14: Reserved */
    b       _start          /* 0x18: IRQ */
    b       _start          /* 0x1C: FIQ */

_start:
    /* ------------------------------------------------------------------
     * Switch to SVC mode, disable IRQ (bit 7) and FIQ (bit 6).
     * CPSR mode bits: 0b10011 = SVC
     * ------------------------------------------------------------------ */
    msr     cpsr_c, #0xD3

    /* ------------------------------------------------------------------
     * Set stack pointer.  _stack_top is defined in the linker script as
     * 0x001FFFFC — the end of our 2 MB working region in DDR3.
     * ------------------------------------------------------------------ */
    ldr     sp, =_stack_top

    /* ------------------------------------------------------------------
     * Zero-initialise .bss.  The linker script exports __bss_start and
     * __bss_end as word-aligned addresses.
     * ------------------------------------------------------------------ */
    ldr     r0, =__bss_start
    ldr     r1, =__bss_end
    mov     r2, #0
.Lbss_loop:
    cmp     r0, r1
    strlt   r2, [r0], #4
    blt     .Lbss_loop

    /* ------------------------------------------------------------------
     * Call main().
     * ------------------------------------------------------------------ */
    bl      main

    /* ------------------------------------------------------------------
     * Halt — main() must never return in a bare-metal system.
     * ------------------------------------------------------------------ */
.Lhalt:
    b       .Lhalt

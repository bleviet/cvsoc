/*
 * startup.S — Minimal ARM Cortex-A9 startup for bare-metal HPS execution.
 *
 * Execution begins at _start immediately after the JTAG debugger (or
 * quartus_hps) transfers control.  This stub:
 *   1. Provides an ARM exception vector table at offset 0.
 *   2. Switches to SVC (Supervisor) mode with IRQ and FIQ disabled.
 *   3. Sets the stack pointer to the top of the 64 KB HPS On-Chip RAM.
 *   4. Zero-initialises the .bss section.
 *   5. Calls main().
 *   6. Loops forever if main() returns.
 *
 * The HPS On-Chip RAM (OCRAM) occupies 0xFFFF0000–0xFFFFFFFF (64 KB).
 * Running from OCRAM requires no SDRAM initialisation and allows fast
 * JTAG-based iteration during development.
 *
 * When L3_REMAP maps OCRAM at 0x00000000, this vector table also serves
 * as the CPU exception vector table.  All exceptions branch to _start so
 * the system re-initialises cleanly on any unexpected trap.
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
     * the byte immediately past the end of the OCRAM region.
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

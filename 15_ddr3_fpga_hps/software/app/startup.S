/*
 * startup.S — Minimal ARM Cortex-A9 startup for DDR3 FPGA+HPS shared test.
 *
 * Identical to 14_ddr3_hps_test — runs from DDR3 at 0x00100000.
 * The U-Boot SPL must initialise DDR3 before this application is loaded.
 */

    .global _start
    .section .startup, "ax", %progbits

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
    msr     cpsr_c, #0xD3

    ldr     sp, =_stack_top

    ldr     r0, =__bss_start
    ldr     r1, =__bss_end
    mov     r2, #0
.Lbss_loop:
    cmp     r0, r1
    strlt   r2, [r0], #4
    blt     .Lbss_loop

    bl      main

.Lhalt:
    b       .Lhalt

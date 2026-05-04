/*
 * startup.S — ARM Cortex-A9 startup for DDR3 FPGA+HPS shared test.
 *
 * Runs from DDR3 at 0x00100000 after U-Boot SPL has initialised the
 * DDR3 controller.  U-Boot leaves the MMU, I-cache, and D-cache enabled,
 * so this startup explicitly brings the CPU to a known clean state before
 * entering C code.
 *
 * Sequence:
 *   1. Supervisor mode, IRQ+FIQ disabled
 *   2. Disable I-cache, D-cache, and MMU via SCTLR
 *   3. Invalidate I-cache (ICIALLU)
 *   4. Set VBAR so exceptions are handled by our vector table
 *   5. Set stack pointer, zero BSS, call main
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
    /* Step 1: Supervisor mode, IRQ+FIQ disabled (CPSR = 0xD3) */
    msr     cpsr_c, #0xD3

    /*
     * Step 2: Disable I-cache (bit 12), D-cache (bit 2), and MMU (bit 0)
     * in SCTLR (CP15 c1 c0 0).
     *
     * U-Boot SPL or U-Boot proper may have enabled all three.  Running with
     * a stale I-cache after a JTAG download causes the CPU to execute old
     * instructions.  Disabling MMU ensures we use physical addresses directly
     * without depending on U-Boot's page tables.
     */
    mrc     p15, 0, r0, c1, c0, 0
    bic     r0, r0, #(1 << 12)          /* I: I-cache enable */
    bic     r0, r0, #((1 << 2) | 1)     /* C: D-cache enable + M: MMU enable */
    mcr     p15, 0, r0, c1, c0, 0
    isb

    /* Step 3: Invalidate entire I-cache (ICIALLU: CP15 c7 c5 0) */
    mov     r0, #0
    mcr     p15, 0, r0, c7, c5, 0
    isb

    /*
     * Step 4: Set VBAR (CP15 c12 c0 0) to _vectors.
     *
     * Without VBAR, the Cortex-A9 fetches exception vectors from 0x00000000,
     * which may contain U-Boot code.  Pointing VBAR at _vectors ensures any
     * fault during initialisation loops back to _start rather than
     * silently executing stale code.
     */
    ldr     r0, =_vectors
    mcr     p15, 0, r0, c12, c0, 0
    isb

    /* Step 5: Set stack pointer from linker symbol */
    ldr     sp, =_stack_top

    /* Step 6: Zero the .bss section */
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

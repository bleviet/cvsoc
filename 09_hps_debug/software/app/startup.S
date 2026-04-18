/*
 * startup.S — ARM Cortex-A9 startup for bare-metal HPS interrupt demo.
 *
 * Changes from 05_hps_led/software/app/startup.S:
 *   1. IRQ vector (offset 0x18) branches to irq_entry instead of _start.
 *   2. Sets up a separate IRQ mode stack before switching to SVC mode.
 *   3. IRQs remain disabled here; main() enables them after GIC configuration.
 *   4. irq_entry saves caller-saved registers, calls irq_c_handler(), returns.
 *
 * Stack layout within OCRAM (64 KB at 0xFFFF0000–0xFFFFFFFF):
 *   _irq_stack_top  = 0xFFFFFFFC  — IRQ mode stack grows down (512 B reserved)
 *   _svc_stack_top  = 0xFFFFFDFC  — SVC mode stack grows down (rest of OCRAM)
 */

    .global _start
    .global irq_c_handler           /* defined in main.c */
    .section .startup, "ax", %progbits

/* ------------------------------------------------------------------
 * ARM Exception Vector Table (at offset 0 of the binary).
 * All unexpected exceptions loop back to _start for re-initialisation.
 * The IRQ vector is directed to irq_entry.
 * ------------------------------------------------------------------ */
_vectors:
    b       _start          /* 0x00: Reset */
    b       _start          /* 0x04: Undefined Instruction */
    b       _start          /* 0x08: Software Interrupt (SVC) */
    b       _start          /* 0x0C: Prefetch Abort */
    b       _start          /* 0x10: Data Abort */
    b       _start          /* 0x14: Reserved */
    b       irq_entry       /* 0x18: IRQ — redirected to interrupt handler */
    b       _start          /* 0x1C: FIQ */

_start:
    /* ------------------------------------------------------------------
     * 1. Set up the IRQ mode stack.
     *    Switch to IRQ mode (0b10010 = 0x12) with IRQ+FIQ disabled.
     *    CPSR[7:6]=11 disables IRQ and FIQ while we initialise stacks.
     * ------------------------------------------------------------------ */
    msr     cpsr_c, #0xD2           /* mode=IRQ, I=1, F=1 */
    ldr     sp, =_irq_stack_top

    /* ------------------------------------------------------------------
     * 2. Switch to SVC (Supervisor) mode, keep IRQ+FIQ disabled.
     *    All subsequent C code executes in SVC mode.
     * ------------------------------------------------------------------ */
    msr     cpsr_c, #0xD3           /* mode=SVC, I=1, F=1 */
    ldr     sp, =_svc_stack_top

    /* ------------------------------------------------------------------
     * 3. Zero-initialise the .bss section.
     * ------------------------------------------------------------------ */
    ldr     r0, =__bss_start
    ldr     r1, =__bss_end
    mov     r2, #0
.Lbss_loop:
    cmp     r0, r1
    strlt   r2, [r0], #4
    blt     .Lbss_loop

    /* ------------------------------------------------------------------
     * 4. Call main().  IRQs are still disabled; main() calls
     *    enable_irq() after the GIC and button_pio are configured.
     * ------------------------------------------------------------------ */
    bl      main

    /* ------------------------------------------------------------------
     * 5. Halt — main() must never return.
     * ------------------------------------------------------------------ */
.Lhalt:
    b       .Lhalt

/* ------------------------------------------------------------------
 * IRQ exception entry stub.
 *
 * Entered in IRQ mode.  LR_irq = address of next instruction + 4.
 *
 * Registers saved/restored:
 *   r0–r3   (caller-saved scratch registers)
 *   r12     (intra-procedure scratch register, ip)
 *   lr      (LR_irq, adjusted by -4 = interrupted PC; becomes return addr)
 *
 * The ^ on ldmfd {…, pc}^ loads SPSR_irq into CPSR, restoring the mode
 * and interrupt flags of the interrupted code (SVC mode, IRQs enabled).
 * GNU as rejects pop^; use ldmfd (full-descending LDM) instead.
 * ------------------------------------------------------------------ */
irq_entry:
    sub     lr, lr, #4              /* Adjust: LR_irq → address of interrupted instr */
    stmfd   sp!, {r0-r3, r12, lr}   /* Save scratch regs + return address on IRQ stack */
    bl      irq_c_handler           /* Call C ISR dispatcher */
    ldmfd   sp!, {r0-r3, r12, pc}^  /* Restore regs, CPSR ← SPSR_irq, return */

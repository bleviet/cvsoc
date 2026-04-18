/*
 * main.c — Nios II GDB debugging demo for the DE10-Nano board.
 *
 * This application is purpose-built for demonstrating source-level GDB
 * debugging of a Nios II soft-core via nios2-gdb-server.
 *
 * Debugging concepts demonstrated:
 *   1. Hardware breakpoints      — 'break set_led'
 *   2. Watchpoints               — 'watch debug_state.step_count'
 *   3. Memory inspection         — 'x/4xw LED_PIO_BASE'
 *   4. Struct inspection         — 'print debug_state'
 *   5. Backtrace from ISR        — 'bt' inside button_isr context
 *   6. Stepping through patterns — 'step', 'next', 'finish'
 *
 * Hardware interrupt chain:
 *   KEY[0:1] (active-LOW) → button_pio (falling-edge capture)
 *     → Nios II IIC IRQ 1 → button_isr()
 *
 * System.h (BSP-generated) defines:
 *   LED_PIO_BASE       0x00010010
 *   BUTTON_PIO_BASE    0x00010020
 *   BUTTON_PIO_IRQ     1
 */

#include <stdint.h>
#include "system.h"
#include "altera_avalon_pio_regs.h"
#include "sys/alt_irq.h"

/* ── Debug state structure ────────────────────────────────────────────────────
 * GDB: print debug_state
 * GDB: watch debug_state.step_count
 */
typedef struct {
    uint8_t  led_pattern;   /* current output pattern */
    uint32_t step_count;    /* number of automatic pattern advances */
    uint32_t irq_count;     /* total interrupts received */
    uint8_t  last_edges;    /* edge bits from last button press */
    uint8_t  pad[3];        /* alignment */
} debug_state_t;

static volatile debug_state_t debug_state = {
    .led_pattern = 0x01u,
    .step_count  = 0u,
    .irq_count   = 0u,
    .last_edges  = 0u,
};

/* Pattern table: inspect with 'x/20ub &patterns' or 'print patterns' */
static const uint8_t patterns[] = {
    0x01, 0x03, 0x07, 0x0F, 0x1F, 0x3F, 0x7F, 0xFF,
    0x7F, 0x3F, 0x1F, 0x0F, 0x07, 0x03, 0x01, 0x00,
    0xAA, 0x55, 0xFF, 0x00,
};
#define NUM_PATTERNS (sizeof(patterns) / sizeof(patterns[0]))

/* ── Helper functions ─────────────────────────────────────────────────────────
 *
 * set_led — primary GDB breakpoint target.
 *
 *   Setting a hardware breakpoint here lets you inspect the pattern
 *   argument and the LED PIO register before/after the write:
 *     (gdb) break set_led
 *     (gdb) commands
 *       print pattern
 *       x/1xw LED_PIO_BASE
 *     end
 *
 * Declared __attribute__((noinline)) so the function always appears as its
 * own frame in the backtrace and GDB can set a reliable breakpoint on it.
 */
static void __attribute__((noinline)) set_led(uint8_t pattern)
{
    IOWR_ALTERA_AVALON_PIO_DATA(LED_PIO_BASE, pattern);
    debug_state.led_pattern = pattern;
}

static void __attribute__((noinline)) delay_ms(uint32_t ms)
{
    volatile uint32_t i, j;
    for (i = 0; i < ms; i++)
        for (j = 0; j < 5000; j++)
            ;
}

/* ── Interrupt Service Routine ────────────────────────────────────────────────
 *
 * process_button — processes captured button edges.
 *
 *   GDB watchpoint on debug_state.irq_count will halt here.
 *   Use 'bt' while halted inside the ISR to see the interrupt call stack.
 *
 * KEY[0]: barrel-rotate LED pattern left one position.
 * KEY[1]: reset step_count to 0 (triggers watchpoint if set).
 */
static void __attribute__((noinline)) process_button(uint8_t edges)
{
    debug_state.last_edges = edges;
    debug_state.irq_count++;

    if (edges & 0x1u) {
        uint8_t p = debug_state.led_pattern;
        debug_state.led_pattern = (uint8_t)((p << 1u) | (p >> 7u));
    }

    if (edges & 0x2u) {
        /* Watchpoint demo: watch debug_state.step_count in GDB, then press KEY[1]. */
        debug_state.step_count = 0u;
    }
}

static void button_isr(void *context)
{
    (void)context;

    uint32_t edges = IORD_ALTERA_AVALON_PIO_EDGE_CAP(BUTTON_PIO_BASE);
    IOWR_ALTERA_AVALON_PIO_EDGE_CAP(BUTTON_PIO_BASE, edges);

    process_button((uint8_t)edges);
}

/* ── Entry point ──────────────────────────────────────────────────────────── */

int main(void)
{
    /* Clear stale edges, arm both buttons, register ISR. */
    IOWR_ALTERA_AVALON_PIO_EDGE_CAP(BUTTON_PIO_BASE, 0x3u);
    IOWR_ALTERA_AVALON_PIO_IRQ_MASK(BUTTON_PIO_BASE, 0x3u);

    alt_ic_isr_register(
        BUTTON_PIO_IRQ_INTERRUPT_CONTROLLER_ID,
        BUTTON_PIO_IRQ,
        button_isr,
        NULL,
        NULL
    );

    /*
     * Main loop: advance through the pattern table, call set_led() each
     * iteration, then sleep.  The loop index rolls over automatically.
     *
     * GDB session example:
     *   (gdb) break set_led          — halt on every LED write
     *   (gdb) watch debug_state.step_count  — halt when step_count changes
     *   (gdb) x/4xw 0x10010         — read LED_PIO_BASE register directly
     */
    while (1) {
        uint32_t idx = debug_state.step_count % NUM_PATTERNS;
        set_led(patterns[idx]);
        debug_state.step_count++;
        delay_ms(300u);
    }

    return 0;
}

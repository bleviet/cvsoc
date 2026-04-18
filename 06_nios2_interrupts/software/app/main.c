/*
 * main.c — Nios II interrupt demo for the DE10-Nano board.
 *
 * Registers an ISR (Interrupt Service Routine) for the button_pio peripheral.
 * Pressing KEY[0] rotates the LED pattern left by one position.
 * Pressing KEY[1] increments a counter whose lower 8 bits are shown on the LEDs.
 *
 * Concepts demonstrated:
 *   - alt_ic_isr_register()  — HAL interrupt registration
 *   - Edge-capture PIO       — interrupt stays asserted until edge reg is cleared
 *   - volatile shared state  — ISR writes, main loop reads (no optimization)
 *   - Interrupt context      — keep ISRs short; no blocking calls inside them
 *
 * Hardware interrupt chain:
 *   KEY[0:1] (active-LOW) → button_pio (falling-edge capture)
 *     → Nios II Internal Interrupt Controller (IIC) → irqNumber 1
 *     → ISR: button_isr()
 *
 * System.h (BSP-generated) defines:
 *   BUTTON_PIO_BASE                      0x00010020
 *   BUTTON_PIO_IRQ                       1
 *   BUTTON_PIO_IRQ_INTERRUPT_CONTROLLER_ID  0
 *   LED_PIO_BASE                         0x00010010
 */

#include <stdint.h>
#include "system.h"
#include "altera_avalon_pio_regs.h"
#include "sys/alt_irq.h"

/* ── Shared state (written by ISR, read by main loop) ─────────────────────── */

/* Current LED display pattern — updated on every button press. */
static volatile uint8_t g_led_pattern = 0x01;

/* Running count of KEY[1] presses — lower 8 bits shown on LEDs after press. */
static volatile uint32_t g_press_count = 0;

/* ── Interrupt Service Routine ────────────────────────────────────────────── */

/*
 * button_isr — handles falling-edge interrupts from KEY[0] and KEY[1].
 *
 * The edge-capture register holds one bit per button: bit 0 = KEY[0],
 * bit 1 = KEY[1].  The register is sticky — it stays set until cleared by
 * writing back the same value (or any value with the affected bits set).
 * Clear BEFORE processing to avoid missing edges that arrive during the ISR.
 *
 * KEY[0]: rotate LED pattern left (barrel shift)
 * KEY[1]: increment press counter; show count on LEDs
 *
 * Keep this routine short.  No printf, no delay, no blocking calls.
 */
static void button_isr(void *context)
{
    (void)context;

    /* Read and atomically clear the edge-capture register. */
    uint32_t edges = IORD_ALTERA_AVALON_PIO_EDGE_CAP(BUTTON_PIO_BASE);
    IOWR_ALTERA_AVALON_PIO_EDGE_CAP(BUTTON_PIO_BASE, edges);

    if (edges & 0x1u) {
        /* KEY[0]: barrel-rotate the LED pattern one position to the left. */
        g_led_pattern = (uint8_t)((g_led_pattern << 1) | (g_led_pattern >> 7));
    }

    if (edges & 0x2u) {
        /* KEY[1]: increment counter; display the new count on the LEDs. */
        g_press_count++;
        g_led_pattern = (uint8_t)g_press_count;
    }
}

/* ── Entry point ──────────────────────────────────────────────────────────── */

int main(void)
{
    /*
     * Clear any stale edges captured before reset, then enable the IRQ mask
     * for both buttons (bit 0 = KEY[0], bit 1 = KEY[1]).
     */
    IOWR_ALTERA_AVALON_PIO_EDGE_CAP(BUTTON_PIO_BASE, 0x3u);
    IOWR_ALTERA_AVALON_PIO_IRQ_MASK(BUTTON_PIO_BASE, 0x3u);

    /*
     * Register button_isr with the HAL.  The HAL enables IRQs globally after
     * the first alt_ic_isr_register() call (the Nios II IIC is always-on).
     */
    alt_ic_isr_register(
        BUTTON_PIO_IRQ_INTERRUPT_CONTROLLER_ID,
        BUTTON_PIO_IRQ,
        button_isr,
        NULL,   /* context — not used */
        NULL    /* flags — reserved, must be NULL */
    );

    /*
     * Main loop: reflect the current LED pattern to the hardware.
     * The pattern is updated asynchronously by button_isr(), so g_led_pattern
     * must be declared volatile to prevent the compiler from caching it in a
     * register.
     */
    while (1) {
        IOWR_ALTERA_AVALON_PIO_DATA(LED_PIO_BASE, g_led_pattern);
    }

    return 0;
}

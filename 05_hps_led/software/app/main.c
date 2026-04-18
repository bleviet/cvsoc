/*
 * main.c — Bare-metal ARM application for the DE10-Nano HPS LED demo.
 *
 * Runs on the ARM Cortex-A9 HPS from On-Chip RAM (OCRAM) at 0xFFFF0000.
 * Controls the 8 on-board LEDs through the FPGA LED PIO peripheral
 * connected via the Lightweight HPS-to-FPGA AXI bridge.
 *
 * Memory map (from the ARM perspective):
 *   H2F Lightweight bridge base : 0xFF200000  (2 MB window)
 *   LED PIO DATA register       : 0xFF200000 + 0x0000 = 0xFF200000
 *
 * Bridge initialisation:
 *   The System Manager (SYSMGR) enables the bridge interface.
 *   The Reset Manager (RSTMGR) releases the bridge from reset.
 *   Both must be configured before the first PIO access.
 */

#include <stdint.h>

/* ── Register addresses (Cyclone V SoC HPS TRM) ─────────────────────────── */

/* System Manager */
#define SYSMGR_BASE             0xFFD08000UL
#define SYSMGR_FPGAINTF_EN_2    (*(volatile uint32_t *)(SYSMGR_BASE + 0x028UL))
#define FPGAINTF_EN2_LWH2F      (1u << 4)   /* enable LW HPS-to-FPGA bridge */

/* Reset Manager */
#define RSTMGR_BASE             0xFFD05000UL
#define RSTMGR_BRGMODRST        (*(volatile uint32_t *)(RSTMGR_BASE + 0x01CUL))
#define BRGMODRST_LWHPS2FPGA    (1u << 2)   /* bridge in reset when set */

/* Lightweight HPS-to-FPGA bridge */
#define H2F_LW_BASE             0xFF200000UL

/* LED PIO registers (altera_avalon_pio, 8-bit output-only) */
#define LED_PIO_DATA            (*(volatile uint32_t *)(H2F_LW_BASE + 0x0000UL))

/* ── Helpers ─────────────────────────────────────────────────────────────── */

/*
 * hps_bridge_init — enable the Lightweight HPS-to-FPGA bridge.
 *
 * After a cold reset the bridges are disabled and held in reset by the
 * Reset Manager.  This function:
 *   1. Enables the LW H2F interface in the System Manager.
 *   2. Releases the LW H2F bridge from reset.
 *
 * The function is idempotent — calling it when the bridge is already active
 * has no side-effects.
 */
static void hps_bridge_init(void)
{
    SYSMGR_FPGAINTF_EN_2 |= FPGAINTF_EN2_LWH2F;
    RSTMGR_BRGMODRST     &= ~BRGMODRST_LWHPS2FPGA;
}

/*
 * delay — busy-loop delay.
 *
 * Runs at roughly 50–100 MHz on the Cortex-A9; 2 000 000 iterations gives
 * approximately 20–40 ms — long enough for the LED pattern to be visible.
 */
static void delay(volatile uint32_t count)
{
    while (count--)
        ;
}

/* ── LED patterns ─────────────────────────────────────────────────────────── */

static const uint8_t patterns[] = {
    /* Fill-up and drain */
    0x01, 0x03, 0x07, 0x0F, 0x1F, 0x3F, 0x7F, 0xFF,
    0x7F, 0x3F, 0x1F, 0x0F, 0x07, 0x03, 0x01, 0x00,
    /* Alternating stripes */
    0xAA, 0x55, 0xAA, 0x55,
    /* All on / all off */
    0xFF, 0x00,
};

/* ── Entry point ──────────────────────────────────────────────────────────── */

void main(void)
{
    uint32_t idx = 0;
    const uint32_t num_patterns = sizeof(patterns) / sizeof(patterns[0]);

    hps_bridge_init();

    while (1) {
        LED_PIO_DATA = patterns[idx];
        idx++;
        if (idx >= num_patterns)
            idx = 0;
        delay(2000000UL);
    }
}

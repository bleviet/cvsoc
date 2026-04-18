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
 *   The Reset Manager (RSTMGR) holds all three AXI bridges in reset after
 *   a cold start.  All three must be released simultaneously — the FPGA
 *   interconnect fabric requires every bridge reset to be de-asserted before
 *   any bridge becomes responsive.
 *   (The SYSMGR FPGAINTF registers control debug/trace/JTAG signals between
 *   the FPGA and HPS, not the AXI bridges, and are not needed here.)
 */

#include <stdint.h>

/* ── Register addresses (Cyclone V SoC HPS TRM) ─────────────────────────── */

/* Reset Manager — bridge module reset register */
#define RSTMGR_BASE             0xFFD05000UL
#define RSTMGR_BRGMODRST        (*(volatile uint32_t *)(RSTMGR_BASE + 0x01CUL))
#define BRGMODRST_HPS2FPGA      (1u << 0)   /* HPS-to-FPGA bridge */
#define BRGMODRST_LWHPS2FPGA    (1u << 1)   /* Lightweight HPS-to-FPGA bridge */
#define BRGMODRST_FPGA2HPS      (1u << 2)   /* FPGA-to-HPS bridge */
#define BRGMODRST_ALL           (BRGMODRST_HPS2FPGA | BRGMODRST_LWHPS2FPGA | BRGMODRST_FPGA2HPS)

/* Lightweight HPS-to-FPGA bridge */
#define H2F_LW_BASE             0xFF200000UL

/* LED PIO registers (altera_avalon_pio, 8-bit output-only) */
#define LED_PIO_DATA            (*(volatile uint32_t *)(H2F_LW_BASE + 0x0000UL))

/* ── Helpers ─────────────────────────────────────────────────────────────── */

static void delay(volatile uint32_t count);

/*
 * hps_bridge_init — toggle all AXI bridges through a full reset cycle.
 *
 * U-Boot SPL releases the bridges early in boot (BRGMODRST = 0), so a plain
 * clear is a no-op.  After JTAG FPGA programming the FPGA fabric's
 * h2f_rst_n-driven reset synchroniser may still be latched.  The only
 * reliable way to de-assert it is to explicitly assert all bridge resets
 * (which drives h2f_rst_n LOW into the FPGA fabric) and then release them.
 * This must be done from the CPU in secure-supervisor mode — MEM-AP writes
 * to RSTMGR are silently discarded (non-secure bus → secured register).
 */
static void hps_bridge_init(void)
{
    /* Assert all bridges — this drives h2f_rst_n LOW into the FPGA fabric. */
    RSTMGR_BRGMODRST |= BRGMODRST_ALL;
    delay(200000UL);    /* ~2 ms at ~100 MHz: let reset propagate */

    /* Release all bridges simultaneously — drives h2f_rst_n HIGH again.
     * The FPGA altera_reset_controller de-asserts reset after 2 clock edges
     * (SYNC_DEPTH = 2 at 50 MHz ≈ 40 ns) so any delay after this is enough. */
    RSTMGR_BRGMODRST &= ~BRGMODRST_ALL;
    delay(200000UL);    /* ~2 ms: let Avalon interconnect come out of reset */
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

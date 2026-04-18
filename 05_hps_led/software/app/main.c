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
 * Bridge initialisation requires TWO steps:
 *   1. L3 (NIC-301) REMAP — set bit 4 to make the LW H2F bridge address
 *      space visible at 0xFF200000.  Without this, any access to the bridge
 *      generates an AXI DECERR (data abort on the CPU).
 *   2. RSTMGR BRGMODRST — assert then release bridge resets to pulse
 *      h2f_rst_n into the FPGA fabric, ensuring the Qsys interconnect and
 *      peripherals come out of reset cleanly after JTAG FPGA programming.
 */

#include <stdint.h>

/* ── Register addresses (Cyclone V SoC HPS TRM) ─────────────────────────── */

/* Watchdog Timers (Synopsys DW APB Watchdog) — must be disabled before they
 * expire and reset the HPS.  U-Boot SPL enables WDT0 with a short timeout;
 * since the enable bit on Cyclone V is NOT write-once, clearing CR[0] works. */
#define WDT0_BASE               0xFFD02000UL
#define WDT1_BASE               0xFFD03000UL
#define WDT_TORR(base)          (*(volatile uint32_t *)((base) + 0x04UL))
#define WDT_CRR(base)           (*(volatile uint32_t *)((base) + 0x0CUL))
#define WDT_KICK_VALUE          0x76u

/* Cortex-A9 MPCore private watchdog (ARM DDI 0407, section 4.4).
 * PERIPHBASE on Cyclone V = 0xFFFEC000 (hardwired). */
#define MPCORE_WDT_BASE         0xFFFEC620UL
#define MPCORE_WDT_LOAD         (*(volatile uint32_t *)(MPCORE_WDT_BASE + 0x00UL))
#define MPCORE_WDT_CTRL         (*(volatile uint32_t *)(MPCORE_WDT_BASE + 0x08UL))
#define MPCORE_WDT_DISABLE      (*(volatile uint32_t *)(MPCORE_WDT_BASE + 0x14UL))

/* Reset Manager — bridge module reset register */
#define RSTMGR_BASE             0xFFD05000UL
#define RSTMGR_BRGMODRST        (*(volatile uint32_t *)(RSTMGR_BASE + 0x01CUL))
#define BRGMODRST_HPS2FPGA      (1u << 0)   /* HPS-to-FPGA bridge */
#define BRGMODRST_LWHPS2FPGA    (1u << 1)   /* Lightweight HPS-to-FPGA bridge */
#define BRGMODRST_FPGA2HPS      (1u << 2)   /* FPGA-to-HPS bridge */
#define BRGMODRST_ALL           (BRGMODRST_HPS2FPGA | BRGMODRST_LWHPS2FPGA | BRGMODRST_FPGA2HPS)

/* L3 (NIC-301) interconnect — remap register.
 * Controls address-space visibility of the HPS-to-FPGA bridges.
 * After cold reset all bridges are invisible; the preloader normally sets
 * this up, but after JTAG FPGA programming the mapping can be lost. */
#define L3_REMAP                (*(volatile uint32_t *)0xFF800000UL)
#define L3_REMAP_OCRAM          (1u << 0)   /* OCRAM at 0x00000000 */
#define L3_REMAP_HPS2FPGA       (1u << 3)   /* H2F bridge at 0xC0000000 */
#define L3_REMAP_LWHPS2FPGA     (1u << 4)   /* LW H2F bridge at 0xFF200000 */

/* Lightweight HPS-to-FPGA bridge */
#define H2F_LW_BASE             0xFF200000UL

/* LED PIO registers (altera_avalon_pio, 8-bit output-only) */
#define LED_PIO_DATA            (*(volatile uint32_t *)(H2F_LW_BASE + 0x0000UL))

/* ── Helpers ─────────────────────────────────────────────────────────────── */

static void delay(volatile uint32_t count);

/*
 * wdt_init — neutralise all watchdog timers on the Cyclone V SoC.
 *
 * On Cyclone V the DW APB Watchdogs have WDT_ALWAYS_EN=1 — the enable bit in
 * WDT_CR is READ-ONLY and permanently set to 1.  These watchdogs CANNOT be
 * disabled; the only way to prevent a warm reset is to:
 *   1. Set the maximum timeout period (TORR = 0xF)
 *   2. Periodically "kick" the counter (write 0x76 to CRR)
 *
 * The Cortex-A9 MPCore private watchdog CAN be disabled via a magic sequence.
 *
 * After calling wdt_init(), the caller must periodically call wdt_kick()
 * before the L4 watchdog timeout expires (~30 s at max TORR with osc1 clock).
 */
static void wdt_init(void)
{
    /* ── L4 Watchdog 0: max timeout + kick ── */
    WDT_TORR(WDT0_BASE) = 0xFFu;            /* TOP=0xF, TOP_INIT=0xF (max) */
    WDT_CRR(WDT0_BASE)  = WDT_KICK_VALUE;  /* restart counter */

    /* ── L4 Watchdog 1: max timeout + kick ── */
    WDT_TORR(WDT1_BASE) = 0xFFu;
    WDT_CRR(WDT1_BASE)  = WDT_KICK_VALUE;

    /* ── Cortex-A9 MPCore Private Watchdog ──
     * Magic sequence switches from watchdog mode to timer mode (no reset).
     * Then clear control register to stop it entirely. */
    MPCORE_WDT_DISABLE = 0x12345678u;
    MPCORE_WDT_DISABLE = 0x87654321u;
    MPCORE_WDT_CTRL    = 0;
}

/* Kick both L4 watchdogs — must be called at least once per timeout period. */
static inline void wdt_kick(void)
{
    WDT_CRR(WDT0_BASE) = WDT_KICK_VALUE;
    WDT_CRR(WDT1_BASE) = WDT_KICK_VALUE;
}

/*
 * hps_bridge_init — enable and reset the Lightweight HPS-to-FPGA bridge.
 *
 * Two things must happen for bridge access to work:
 *
 * 1. L3 REMAP — set bit 4 so the LW bridge address window (0xFF200000)
 *    is visible in the CPU address map.  Without this, the L3 interconnect
 *    returns DECERR for any access in that range.
 *
 * 2. BRGMODRST toggle — assert then release bridge resets.  This pulses
 *    h2f_rst_n LOW→HIGH into the FPGA fabric, ensuring the Qsys reset
 *    synchroniser properly de-asserts and all Avalon slaves come out of
 *    reset after JTAG FPGA programming.
 */
static void hps_bridge_init(void)
{
    /* Enable LW H2F bridge address visibility in L3 interconnect */
    L3_REMAP = L3_REMAP_LWHPS2FPGA | L3_REMAP_OCRAM;

    /* Assert all bridges — drives h2f_rst_n LOW into the FPGA fabric */
    RSTMGR_BRGMODRST |= BRGMODRST_ALL;
    delay(200000UL);

    /* Release all bridges — drives h2f_rst_n HIGH; Qsys interconnect and
     * PIO come out of reset after ~2 clock edges at 50 MHz (≈ 40 ns). */
    RSTMGR_BRGMODRST &= ~BRGMODRST_ALL;
    delay(200000UL);
}

/*
 * delay — busy-loop delay with watchdog kick.
 *
 * Kicks the L4 watchdogs at entry to prevent timeout during long delays.
 * Each call at count=2000000 takes ~100–200 ms on the Cortex-A9 at 100 MHz.
 */
static void delay(volatile uint32_t count)
{
    /* Kick watchdogs at entry — prevents timeout during long waits */
    WDT_CRR(WDT0_BASE) = WDT_KICK_VALUE;
    WDT_CRR(WDT1_BASE) = WDT_KICK_VALUE;
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

    wdt_init();
    hps_bridge_init();

    while (1) {
        wdt_kick();
        LED_PIO_DATA = patterns[idx];
        idx++;
        if (idx >= num_patterns)
            idx = 0;
        delay(2000000UL);
    }
}

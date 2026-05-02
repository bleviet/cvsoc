/*
 * main.c — Bare-metal DDR3 SDRAM memory test for the DE10-Nano.
 *
 * Runs on the ARM Cortex-A9 HPS from DDR3 SDRAM at 0x00100000.
 * The DDR3 controller must be initialised by U-Boot SPL before this
 * application is loaded (via JTAG or U-Boot 'go' command).
 *
 * Memory map:
 *   0x00000000 – 0x000FFFFF  Reserved (SPL/U-Boot workspace)
 *   0x00100000 – 0x001FFFFF  This application (code + data + stack)
 *   0x00200000 – 0x03FFFFFF  DDR3 test region (62 MB)
 *
 * LED behaviour:
 *   Running pattern  → test in progress
 *   All 8 LEDs on    → all tests passed
 *   Alternating blink → test failed (LED pattern shows which test failed)
 *
 * Test patterns implemented:
 *   1. Walking ones        — single '1' bit walks through each bit position
 *   2. Address-as-data     — each address stores its own value
 *   3. Alternating patterns — 0xAAAAAAAA and 0x55555555
 */

#include <stdint.h>

/* ── Register addresses (Cyclone V SoC HPS TRM) ─────────────────────────── */

/* Watchdog Timers */
#define WDT0_BASE               0xFFD02000UL
#define WDT1_BASE               0xFFD03000UL
#define WDT_TORR(base)          (*(volatile uint32_t *)((base) + 0x04UL))
#define WDT_CRR(base)           (*(volatile uint32_t *)((base) + 0x0CUL))
#define WDT_KICK_VALUE          0x76u

/* Cortex-A9 MPCore private watchdog */
#define MPCORE_WDT_BASE         0xFFFEC620UL
#define MPCORE_WDT_LOAD         (*(volatile uint32_t *)(MPCORE_WDT_BASE + 0x00UL))
#define MPCORE_WDT_CTRL         (*(volatile uint32_t *)(MPCORE_WDT_BASE + 0x08UL))
#define MPCORE_WDT_DISABLE      (*(volatile uint32_t *)(MPCORE_WDT_BASE + 0x14UL))

/* Reset Manager */
#define RSTMGR_BASE             0xFFD05000UL
#define RSTMGR_BRGMODRST        (*(volatile uint32_t *)(RSTMGR_BASE + 0x01CUL))
#define BRGMODRST_HPS2FPGA      (1u << 0)
#define BRGMODRST_LWHPS2FPGA    (1u << 1)
#define BRGMODRST_FPGA2HPS      (1u << 2)
#define BRGMODRST_ALL           (BRGMODRST_HPS2FPGA | BRGMODRST_LWHPS2FPGA | BRGMODRST_FPGA2HPS)

/* L3 (NIC-301) interconnect — remap register */
#define L3_REMAP                (*(volatile uint32_t *)0xFF800000UL)
#define L3_REMAP_OCRAM          (1u << 0)
#define L3_REMAP_HPS2FPGA       (1u << 3)
#define L3_REMAP_LWHPS2FPGA     (1u << 4)

/* Lightweight HPS-to-FPGA bridge */
#define H2F_LW_BASE             0xFF200000UL

/* LED PIO register */
#define LED_PIO_DATA            (*(volatile uint32_t *)(H2F_LW_BASE + 0x0000UL))

/* ── DDR3 test region ────────────────────────────────────────────────────── */

/* Test region starts at 2 MB offset, well past our application.
 * Test size: 62 MB (0x00200000 – 0x03FFFFFF). */
#define DDR3_TEST_BASE          0x00200000UL
#define DDR3_TEST_SIZE_BYTES    (62UL * 1024UL * 1024UL)    /* 62 MB */
#define DDR3_TEST_SIZE_WORDS    (DDR3_TEST_SIZE_BYTES / 4UL)

/* ── Test result codes ───────────────────────────────────────────────────── */
#define TEST_PASS               0u
#define TEST_FAIL_WALKING_ONES  (1u << 0)
#define TEST_FAIL_ADDR_AS_DATA  (1u << 1)
#define TEST_FAIL_ALT_PATTERN   (1u << 2)

/* ── Helpers ─────────────────────────────────────────────────────────────── */

static void delay(volatile uint32_t count);

static void wdt_init(void)
{
    /* L4 Watchdog 0: max timeout + kick */
    WDT_TORR(WDT0_BASE) = 0xFFu;
    WDT_CRR(WDT0_BASE)  = WDT_KICK_VALUE;

    /* L4 Watchdog 1: max timeout + kick */
    WDT_TORR(WDT1_BASE) = 0xFFu;
    WDT_CRR(WDT1_BASE)  = WDT_KICK_VALUE;

    /* Cortex-A9 MPCore Private Watchdog — disable */
    MPCORE_WDT_DISABLE = 0x12345678u;
    MPCORE_WDT_DISABLE = 0x87654321u;
    MPCORE_WDT_CTRL    = 0;
}

static inline void wdt_kick(void)
{
    WDT_CRR(WDT0_BASE) = WDT_KICK_VALUE;
    WDT_CRR(WDT1_BASE) = WDT_KICK_VALUE;
}

static void hps_bridge_init(void)
{
    /* Enable LW H2F bridge address visibility in L3 interconnect */
    L3_REMAP = L3_REMAP_LWHPS2FPGA | L3_REMAP_OCRAM;

    /* Assert then release bridge resets */
    RSTMGR_BRGMODRST |= BRGMODRST_ALL;
    delay(200000UL);
    RSTMGR_BRGMODRST &= ~BRGMODRST_ALL;
    delay(200000UL);
}

static void delay(volatile uint32_t count)
{
    WDT_CRR(WDT0_BASE) = WDT_KICK_VALUE;
    WDT_CRR(WDT1_BASE) = WDT_KICK_VALUE;
    while (count--)
        ;
}

/* ── LED progress indicator ──────────────────────────────────────────────── */

/* Show a running light pattern to indicate which test phase is active.
 * phase_idx: 0=walking_ones, 1=addr_as_data, 2=alt_pattern */
static void led_show_progress(uint32_t phase_idx, uint32_t step)
{
    /* Shift a lit LED based on step; phase sets the base pattern */
    uint8_t pattern = (uint8_t)(1u << (step & 7u));
    /* Upper bits indicate phase: phase 0 = no extra, 1 = bit 7, 2 = bits 7+6 */
    if (phase_idx >= 1)
        pattern |= 0x80u;
    if (phase_idx >= 2)
        pattern |= 0xC0u;
    LED_PIO_DATA = pattern;
}

static void led_show_pass(void)
{
    LED_PIO_DATA = 0xFFu;  /* All 8 LEDs on */
}

static void led_show_fail(uint32_t fail_mask)
{
    /* Blink the fail_mask pattern forever */
    while (1) {
        wdt_kick();
        LED_PIO_DATA = (uint8_t)(fail_mask & 0xFFu);
        delay(2000000UL);
        LED_PIO_DATA = 0x00u;
        delay(2000000UL);
    }
}

/* ── Test 1: Walking Ones ────────────────────────────────────────────────── */
/*
 * At each test address, write a single '1' bit that walks through all 32
 * bit positions.  Read back and verify after each write.  This detects
 * stuck-at faults on individual data lines.
 *
 * To keep the test manageable in time, we test a subset of addresses:
 * one address per 4 KB page across the test region.
 */
static uint32_t test_walking_ones(void)
{
    volatile uint32_t *base = (volatile uint32_t *)DDR3_TEST_BASE;
    uint32_t num_pages = DDR3_TEST_SIZE_BYTES / 4096UL;
    uint32_t page, bit;
    uint32_t step = 0;

    for (page = 0; page < num_pages; page++) {
        volatile uint32_t *addr = base + (page * (4096UL / 4UL));

        for (bit = 0; bit < 32; bit++) {
            uint32_t pattern = 1u << bit;

            *addr = pattern;

            /* Data Synchronization Barrier — ensure write completes */
            __asm__ volatile ("dsb" ::: "memory");

            uint32_t readback = *addr;
            if (readback != pattern)
                return TEST_FAIL_WALKING_ONES;
        }

        /* Update LED progress every 256 pages (~1 MB) */
        if ((page & 0xFFu) == 0) {
            led_show_progress(0, step++);
            wdt_kick();
        }
    }

    return TEST_PASS;
}

/* ── Test 2: Address-as-Data ─────────────────────────────────────────────── */
/*
 * Write each address's own value as its data word across the entire test
 * region, then read everything back in a second pass.  This detects
 * address-line faults (solder bridges, open pins) and data retention issues.
 */
static uint32_t test_address_as_data(void)
{
    volatile uint32_t *base = (volatile uint32_t *)DDR3_TEST_BASE;
    uint32_t i;
    uint32_t step = 0;

    /* Write pass */
    for (i = 0; i < DDR3_TEST_SIZE_WORDS; i++) {
        base[i] = DDR3_TEST_BASE + (i * 4u);

        if ((i & 0xFFFFFu) == 0) {         /* every 4 MB */
            led_show_progress(1, step++);
            wdt_kick();
        }
    }

    /* Flush all writes */
    __asm__ volatile ("dsb" ::: "memory");

    /* Read-back pass */
    step = 0;
    for (i = 0; i < DDR3_TEST_SIZE_WORDS; i++) {
        uint32_t expected = DDR3_TEST_BASE + (i * 4u);
        uint32_t actual   = base[i];

        if (actual != expected)
            return TEST_FAIL_ADDR_AS_DATA;

        if ((i & 0xFFFFFu) == 0) {
            led_show_progress(1, step++);
            wdt_kick();
        }
    }

    return TEST_PASS;
}

/* ── Test 3: Alternating Patterns ────────────────────────────────────────── */
/*
 * Fill the entire test region with 0xAAAAAAAA, read back, then fill with
 * 0x55555555 and read back.  This detects coupling faults between adjacent
 * data lines (each pattern puts neighbouring bits in opposite states).
 */
static uint32_t test_alternating_patterns(void)
{
    volatile uint32_t *base = (volatile uint32_t *)DDR3_TEST_BASE;
    uint32_t i;
    uint32_t step = 0;
    const uint32_t patterns[2] = { 0xAAAAAAAAu, 0x55555555u };
    uint32_t p;

    for (p = 0; p < 2; p++) {
        uint32_t pat = patterns[p];

        /* Write pass */
        for (i = 0; i < DDR3_TEST_SIZE_WORDS; i++) {
            base[i] = pat;

            if ((i & 0xFFFFFu) == 0) {
                led_show_progress(2, step++);
                wdt_kick();
            }
        }

        __asm__ volatile ("dsb" ::: "memory");

        /* Read-back pass */
        for (i = 0; i < DDR3_TEST_SIZE_WORDS; i++) {
            uint32_t actual = base[i];

            if (actual != pat)
                return TEST_FAIL_ALT_PATTERN;

            if ((i & 0xFFFFFu) == 0) {
                led_show_progress(2, step++);
                wdt_kick();
            }
        }
    }

    return TEST_PASS;
}

/* ── Entry point ──────────────────────────────────────────────────────────── */

void main(void)
{
    uint32_t result = TEST_PASS;

    wdt_init();
    hps_bridge_init();

    /* Clear all LEDs */
    LED_PIO_DATA = 0x00u;
    delay(1000000UL);

    /* ── Run test suite ────────────────────────────────────────────────── */

    /* Test 1: Walking ones */
    result = test_walking_ones();
    if (result != TEST_PASS)
        led_show_fail(result);  /* never returns */

    /* Test 2: Address-as-data */
    result = test_address_as_data();
    if (result != TEST_PASS)
        led_show_fail(result);

    /* Test 3: Alternating patterns */
    result = test_alternating_patterns();
    if (result != TEST_PASS)
        led_show_fail(result);

    /* ── All tests passed ──────────────────────────────────────────────── */
    led_show_pass();

    /* Hold all LEDs on; kick watchdogs forever */
    while (1) {
        wdt_kick();
        delay(5000000UL);
    }
}

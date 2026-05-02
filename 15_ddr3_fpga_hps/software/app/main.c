/*
 * main.c — Shared DDR3 access test: FPGA and HPS both read/write DDR3.
 *
 * Runs on the ARM Cortex-A9 HPS from DDR3 SDRAM at 0x00100000.
 * The FPGA fabric has a ddr3_test_master component connected to the
 * FPGA-to-HPS SDRAM bridge (f2h_sdram0), allowing it to independently
 * read/write DDR3 memory.
 *
 * Test procedure (producer-consumer pattern):
 *   Phase 1: HPS writes pattern A to region A (0x00200000, 1 MB)
 *   Phase 2: FPGA writes pattern B to region B (0x03000000, 1 MB)
 *            (HPS waits for FPGA to finish)
 *   Phase 3: HPS reads region B and verifies FPGA wrote correctly
 *   Phase 4: FPGA reads region A and verifies HPS wrote correctly
 *            (HPS waits for FPGA to finish, then checks error count)
 *
 * LED behaviour:
 *   0x01 (LED 0)     → Phase 1: HPS writing region A
 *   0x03 (LED 0,1)   → Phase 2: FPGA writing region B
 *   0x07 (LED 0,1,2) → Phase 3: HPS verifying region B
 *   0x0F (LED 0-3)   → Phase 4: FPGA verifying region A
 *   0xFF (all on)     → All tests passed
 *   Blinking          → Test failed
 *
 * Memory map:
 *   0x00100000 – 0x001FFFFF  This application (code + data + stack)
 *   0x00200000 – 0x002FFFFF  Region A — written by HPS, verified by FPGA (1 MB)
 *   0x03000000 – 0x030FFFFF  Region B — written by FPGA, verified by HPS (1 MB)
 */

#include <stdint.h>

/* ── Register addresses ──────────────────────────────────────────────────── */

#define WDT0_BASE               0xFFD02000UL
#define WDT1_BASE               0xFFD03000UL
#define WDT_TORR(base)          (*(volatile uint32_t *)((base) + 0x04UL))
#define WDT_CRR(base)           (*(volatile uint32_t *)((base) + 0x0CUL))
#define WDT_KICK_VALUE          0x76u

#define MPCORE_WDT_BASE         0xFFFEC620UL
#define MPCORE_WDT_CTRL         (*(volatile uint32_t *)(MPCORE_WDT_BASE + 0x08UL))
#define MPCORE_WDT_DISABLE      (*(volatile uint32_t *)(MPCORE_WDT_BASE + 0x14UL))

#define RSTMGR_BASE             0xFFD05000UL
#define RSTMGR_BRGMODRST        (*(volatile uint32_t *)(RSTMGR_BASE + 0x01CUL))
#define BRGMODRST_HPS2FPGA      (1u << 0)
#define BRGMODRST_LWHPS2FPGA    (1u << 1)
#define BRGMODRST_FPGA2HPS      (1u << 2)
#define BRGMODRST_ALL           (BRGMODRST_HPS2FPGA | BRGMODRST_LWHPS2FPGA | BRGMODRST_FPGA2HPS)

#define L3_REMAP                (*(volatile uint32_t *)0xFF800000UL)
#define L3_REMAP_OCRAM          (1u << 0)
#define L3_REMAP_HPS2FPGA       (1u << 3)
#define L3_REMAP_LWHPS2FPGA     (1u << 4)

#define H2F_LW_BASE             0xFF200000UL

/* LED PIO */
#define LED_PIO_DATA            (*(volatile uint32_t *)(H2F_LW_BASE + 0x0000UL))

/* ── DDR3 test master control registers (on LW bridge at offset 0x1000) ── */
#define DDR3_MASTER_BASE        (H2F_LW_BASE + 0x1000UL)
#define DDR3_MASTER_CTRL        (*(volatile uint32_t *)(DDR3_MASTER_BASE + 0x00UL))
#define DDR3_MASTER_BASE_ADDR   (*(volatile uint32_t *)(DDR3_MASTER_BASE + 0x04UL))
#define DDR3_MASTER_LENGTH      (*(volatile uint32_t *)(DDR3_MASTER_BASE + 0x08UL))
#define DDR3_MASTER_STATUS      (*(volatile uint32_t *)(DDR3_MASTER_BASE + 0x0CUL))
#define DDR3_MASTER_PATTERN     (*(volatile uint32_t *)(DDR3_MASTER_BASE + 0x10UL))

/* CTRL register bits */
#define CTRL_START              (1u << 0)
#define CTRL_MODE_READ          (1u << 1)   /* 0=write, 1=read+verify */
#define CTRL_RUNNING            (1u << 2)   /* read-only */

/* STATUS register bits */
#define STATUS_DONE             (1u << 0)
#define STATUS_ERROR            (1u << 1)
#define STATUS_ERROR_COUNT_MASK 0xFFFF0000u
#define STATUS_ERROR_COUNT_SHIFT 16

/* ── Test configuration ──────────────────────────────────────────────────── */

/* Region A: written by HPS, verified by FPGA */
#define REGION_A_BASE           0x00200000UL
#define REGION_A_SIZE_WORDS     (1UL * 1024UL * 1024UL / 4UL)  /* 1 MB = 256K words */
#define PATTERN_A               0xCAFE0000UL

/* Region B: written by FPGA, verified by HPS */
#define REGION_B_BASE           0x03000000UL
#define REGION_B_SIZE_WORDS     (1UL * 1024UL * 1024UL / 4UL)  /* 1 MB = 256K words */
#define PATTERN_B               0xBEEF0000UL

/* ── Helpers ─────────────────────────────────────────────────────────────── */

static void delay(volatile uint32_t count);

static void wdt_init(void)
{
    WDT_TORR(WDT0_BASE) = 0xFFu;
    WDT_CRR(WDT0_BASE)  = WDT_KICK_VALUE;
    WDT_TORR(WDT1_BASE) = 0xFFu;
    WDT_CRR(WDT1_BASE)  = WDT_KICK_VALUE;

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
    /* Enable both LW H2F bridge and FPGA-to-HPS bridge visibility */
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

static void led_show_fail(uint8_t phase_led)
{
    while (1) {
        wdt_kick();
        LED_PIO_DATA = phase_led;
        delay(2000000UL);
        LED_PIO_DATA = 0x00u;
        delay(2000000UL);
    }
}

/* ── FPGA test master helpers ────────────────────────────────────────────── */

/*
 * Start the FPGA DDR3 test master in write mode.
 * The master writes (pattern + word_offset) to each address.
 */
static void fpga_master_start_write(uint32_t base_addr, uint32_t num_words,
                                     uint32_t pattern)
{
    DDR3_MASTER_BASE_ADDR = base_addr;
    DDR3_MASTER_LENGTH    = num_words;
    DDR3_MASTER_PATTERN   = pattern;
    /* Start in write mode */
    DDR3_MASTER_CTRL      = CTRL_START;
}

/*
 * Start the FPGA DDR3 test master in read+verify mode.
 * The master reads each address and compares against (pattern + word_offset).
 */
static void fpga_master_start_read(uint32_t base_addr, uint32_t num_words,
                                    uint32_t pattern)
{
    DDR3_MASTER_BASE_ADDR = base_addr;
    DDR3_MASTER_LENGTH    = num_words;
    DDR3_MASTER_PATTERN   = pattern;
    /* Start in read mode */
    DDR3_MASTER_CTRL      = CTRL_START | CTRL_MODE_READ;
}

/*
 * Wait for the FPGA test master to complete.
 * Returns the number of errors (0 = pass).
 */
static uint32_t fpga_master_wait(void)
{
    while (1) {
        wdt_kick();
        uint32_t status = DDR3_MASTER_STATUS;
        if (status & STATUS_DONE) {
            if (status & STATUS_ERROR) {
                return (status & STATUS_ERROR_COUNT_MASK) >> STATUS_ERROR_COUNT_SHIFT;
            }
            return 0;
        }
    }
}

/* ── Entry point ──────────────────────────────────────────────────────────── */

void main(void)
{
    wdt_init();
    hps_bridge_init();

    /* Clear all LEDs */
    LED_PIO_DATA = 0x00u;
    delay(1000000UL);

    /* ── Phase 1: HPS writes pattern A to region A ─────────────────────── */
    LED_PIO_DATA = 0x01u;  /* LED 0 on */
    {
        volatile uint32_t *base = (volatile uint32_t *)REGION_A_BASE;
        for (uint32_t i = 0; i < REGION_A_SIZE_WORDS; i++) {
            base[i] = PATTERN_A + i;
            if ((i & 0xFFFFu) == 0)
                wdt_kick();
        }
        __asm__ volatile ("dsb" ::: "memory");
    }

    /* ── Phase 2: FPGA writes pattern B to region B ────────────────────── */
    LED_PIO_DATA = 0x03u;  /* LED 0,1 on */
    fpga_master_start_write(REGION_B_BASE, REGION_B_SIZE_WORDS, PATTERN_B);
    fpga_master_wait();  /* wait for FPGA to finish writing */

    /* ── Phase 3: HPS reads region B, verifies FPGA wrote correctly ────── */
    LED_PIO_DATA = 0x07u;  /* LED 0,1,2 on */
    {
        volatile uint32_t *base = (volatile uint32_t *)REGION_B_BASE;
        for (uint32_t i = 0; i < REGION_B_SIZE_WORDS; i++) {
            uint32_t expected = PATTERN_B + i;
            uint32_t actual   = base[i];
            if (actual != expected)
                led_show_fail(0x07u);  /* blink LED 0,1,2 — never returns */
            if ((i & 0xFFFFu) == 0)
                wdt_kick();
        }
    }

    /* ── Phase 4: FPGA reads region A, verifies HPS wrote correctly ────── */
    LED_PIO_DATA = 0x0Fu;  /* LED 0,1,2,3 on */
    fpga_master_start_read(REGION_A_BASE, REGION_A_SIZE_WORDS, PATTERN_A);
    {
        uint32_t fpga_errors = fpga_master_wait();
        if (fpga_errors != 0)
            led_show_fail(0x0Fu);  /* blink LED 0-3 — never returns */
    }

    /* ── All tests passed ──────────────────────────────────────────────── */
    LED_PIO_DATA = 0xFFu;  /* All 8 LEDs on */

    while (1) {
        wdt_kick();
        delay(5000000UL);
    }
}

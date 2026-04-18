/*
 * main.c — ARM HPS GDB debugging demo for the DE10-Nano board.
 *
 * Extends 07_hps_interrupts to make specific debugging scenarios easy to
 * trigger via GDB.  Runs on the ARM Cortex-A9 from HPS OCRAM (0xFFFF0000).
 *
 * Debugging concepts demonstrated:
 *   1. Breakpoint at boot          — 'hbreak _start' before reset
 *   2. Stepping through startup.S  — 'si' (stepi) through exception setup
 *   3. GIC register inspection     — 'x/4xw 0xFFFED000' (GICD base)
 *   4. Watchpoints                 — 'watch g_debug.led_pattern'
 *   5. Struct inspection           — 'print g_debug'
 *   6. Avalon register via GDB     — 'x/1xw 0xFF200000' (LED PIO DATA)
 *   7. IRQ frame backtrace         — 'bt' while inside irq_c_handler
 *
 * Memory map (ARM perspective):
 *   OCRAM         : 0xFFFF0000 (64 KB — code, data, stacks)
 *   GIC GICD      : 0xFFFED000
 *   GIC GICC      : 0xFFFEC100
 *   LW H2F bridge : 0xFF200000
 *   LED PIO DATA  : 0xFF200000
 *   Button PIO    : 0xFF201000
 *
 * FPGA-to-HPS interrupt:
 *   button_pio.irq → f2h_irq0[0] → GIC SPI[40] → ID 72
 */

#include <stdint.h>

/* ── Register addresses ───────────────────────────────────────────────────── */

#define WDT0_BASE           0xFFD02000UL
#define WDT1_BASE           0xFFD03000UL
#define WDT_TORR(b)         (*(volatile uint32_t *)((b) + 0x04UL))
#define WDT_CRR(b)          (*(volatile uint32_t *)((b) + 0x0CUL))
#define WDT_KICK_VALUE      0x76u

#define MPCORE_WDT_BASE     0xFFFEC620UL
#define MPCORE_WDT_LOAD     (*(volatile uint32_t *)(MPCORE_WDT_BASE + 0x00UL))
#define MPCORE_WDT_CTRL     (*(volatile uint32_t *)(MPCORE_WDT_BASE + 0x08UL))
#define MPCORE_WDT_DISABLE  (*(volatile uint32_t *)(MPCORE_WDT_BASE + 0x14UL))

#define RSTMGR_BASE             0xFFD05000UL
#define RSTMGR_BRGMODRST        (*(volatile uint32_t *)(RSTMGR_BASE + 0x01CUL))
#define BRGMODRST_HPS2FPGA      (1u << 0)
#define BRGMODRST_LWHPS2FPGA    (1u << 1)
#define BRGMODRST_FPGA2HPS      (1u << 2)
#define BRGMODRST_ALL           (BRGMODRST_HPS2FPGA | BRGMODRST_LWHPS2FPGA | BRGMODRST_FPGA2HPS)

#define L3_REMAP                (*(volatile uint32_t *)0xFF800000UL)
#define L3_REMAP_OCRAM          (1u << 0)
#define L3_REMAP_LWHPS2FPGA     (1u << 4)

#define H2F_LW_BASE             0xFF200000UL

/* LED PIO — GDB: x/1xw 0xFF200000 */
#define LED_PIO_DATA    (*(volatile uint32_t *)(H2F_LW_BASE + 0x0000UL))

/* Button PIO — GDB: x/4xw 0xFF201000 */
#define BUTTON_PIO_BASE     (H2F_LW_BASE + 0x1000UL)
#define BUTTON_PIO_DATA     (*(volatile uint32_t *)(BUTTON_PIO_BASE + 0x00UL))
#define BUTTON_PIO_IRQ_MASK (*(volatile uint32_t *)(BUTTON_PIO_BASE + 0x08UL))
#define BUTTON_PIO_EDGE_CAP (*(volatile uint32_t *)(BUTTON_PIO_BASE + 0x0CUL))

/* GIC — GDB: x/4xw 0xFFFED000 (GICD) or x/4xw 0xFFFEC100 (GICC) */
#define GICD_BASE   0xFFFED000UL
#define GICC_BASE   0xFFFEC100UL

#define GICD_CTLR           (*(volatile uint32_t *)(GICD_BASE + 0x000UL))
#define GICD_ISENABLER(n)   (*(volatile uint32_t *)(GICD_BASE + 0x100UL + 4u*(n)))
#define GICD_ICENABLER(n)   (*(volatile uint32_t *)(GICD_BASE + 0x180UL + 4u*(n)))
#define GICD_IPRIORITYR(n)  (*(volatile uint8_t  *)(GICD_BASE + 0x400UL + (n)))
#define GICD_ITARGETSR(n)   (*(volatile uint8_t  *)(GICD_BASE + 0x800UL + (n)))

#define GICC_CTLR   (*(volatile uint32_t *)(GICC_BASE + 0x000UL))
#define GICC_PMR    (*(volatile uint32_t *)(GICC_BASE + 0x004UL))
#define GICC_IAR    (*(volatile uint32_t *)(GICC_BASE + 0x00CUL))
#define GICC_EOIR   (*(volatile uint32_t *)(GICC_BASE + 0x010UL))

#define BUTTON_GIC_IRQ  72u

/* ── Debug info structure ─────────────────────────────────────────────────────
 *
 * GDB: print g_debug
 * GDB: watch g_debug.led_pattern
 * GDB: watch g_debug.irq_count
 *
 * The struct is exported (non-static) so GDB can always find it by name
 * regardless of link-time optimization.
 */
typedef struct {
    uint8_t  led_pattern;       /* current LED output */
    uint8_t  pad[3];
    uint32_t step_count;        /* automatic pattern advances in main loop */
    uint32_t irq_count;         /* total GIC interrupts handled */
    uint32_t last_irq_id;       /* GIC interrupt ID of last IRQ (for inspection) */
    uint8_t  last_edges;        /* button_pio edge bits of last button press */
    uint8_t  pad2[3];
} debug_info_t;

volatile debug_info_t g_debug = {
    .led_pattern = 0x01u,
    .step_count  = 0u,
    .irq_count   = 0u,
    .last_irq_id = 0u,
    .last_edges  = 0u,
};

/* Pattern table */
static const uint8_t patterns[] = {
    0x01, 0x03, 0x07, 0x0F, 0x1F, 0x3F, 0x7F, 0xFF,
    0x7F, 0x3F, 0x1F, 0x0F, 0x07, 0x03, 0x01, 0x00,
    0xAA, 0x55, 0xFF, 0x00,
};
#define NUM_PATTERNS (sizeof(patterns) / sizeof(patterns[0]))

/* ── Forward declarations ─────────────────────────────────────────────────── */
static void wdt_init(void);
static void wdt_kick(void);
static void hps_bridge_init(void);
static void gic_init(void);
static void button_pio_init(void);
static void delay(volatile uint32_t count);

/* ── IRQ C handler ────────────────────────────────────────────────────────────
 *
 * Called from irq_entry in startup.S.
 *
 * GDB: set a breakpoint here with 'hbreak irq_c_handler', then press a button.
 * GDB: Use 'print g_debug' to inspect state; 'bt' to see irq_entry → here.
 * GDB: Inspect GIC CPU interface: 'x/4xw 0xFFFEC100'
 */
void __attribute__((noinline)) irq_c_handler(void)
{
    uint32_t iar    = GICC_IAR;
    uint32_t irq_id = iar & 0x3FFu;

    g_debug.last_irq_id = irq_id;

    if (irq_id == BUTTON_GIC_IRQ) {
        uint32_t edges = BUTTON_PIO_EDGE_CAP;
        BUTTON_PIO_EDGE_CAP = edges;

        g_debug.last_edges = (uint8_t)edges;
        g_debug.irq_count++;

        if (edges & 0x1u) {
            uint8_t p = g_debug.led_pattern;
            g_debug.led_pattern = (uint8_t)((p << 1u) | (p >> 7u));
        }

        if (edges & 0x2u) {
            /* Watchpoint demo: 'watch g_debug.step_count', press KEY[1]. */
            g_debug.step_count = 0u;
        }
    }

    GICC_EOIR = iar;
}

/* ── Watchdog ─────────────────────────────────────────────────────────────── */

static void wdt_init(void)
{
    WDT_TORR(WDT0_BASE) = 0xFFu;
    WDT_CRR(WDT0_BASE)  = WDT_KICK_VALUE;
    WDT_TORR(WDT1_BASE) = 0xFFu;
    WDT_CRR(WDT1_BASE)  = WDT_KICK_VALUE;

    MPCORE_WDT_DISABLE = 0x12345678u;
    MPCORE_WDT_DISABLE = 0x87654321u;
    MPCORE_WDT_CTRL    = 0u;
}

static inline void wdt_kick(void)
{
    WDT_CRR(WDT0_BASE) = WDT_KICK_VALUE;
    WDT_CRR(WDT1_BASE) = WDT_KICK_VALUE;
}

/* ── Bridge initialisation ────────────────────────────────────────────────── */

static void hps_bridge_init(void)
{
    L3_REMAP = L3_REMAP_LWHPS2FPGA | L3_REMAP_OCRAM;

    RSTMGR_BRGMODRST |= BRGMODRST_ALL;
    delay(200000UL);
    RSTMGR_BRGMODRST &= ~BRGMODRST_ALL;
    delay(200000UL);
}

/* ── GIC initialisation ───────────────────────────────────────────────────────
 *
 * After gic_init() returns, inspect the GIC distributor registers:
 *   (gdb) x/2xw 0xFFFED100        # GICD_ISENABLER[0..1]
 *   (gdb) x/1xb 0xFFFED460        # GICD_IPRIORITYR[72]
 *   (gdb) x/1xb 0xFFFED848        # GICD_ITARGETSR[72]
 */
static void gic_init(void)
{
    GICD_CTLR = 0u;

    GICD_IPRIORITYR(BUTTON_GIC_IRQ) = 0xA0u;
    GICD_ITARGETSR(BUTTON_GIC_IRQ)  = 0x01u;
    GICD_ISENABLER(BUTTON_GIC_IRQ / 32u) = (1u << (BUTTON_GIC_IRQ % 32u));

    GICD_CTLR = 1u;
    __asm__ volatile("dsb" ::: "memory");

    GICC_PMR  = 0xF0u;
    GICC_CTLR = 1u;
}

/* ── Button PIO ───────────────────────────────────────────────────────────── */

static void button_pio_init(void)
{
    BUTTON_PIO_EDGE_CAP = 0x3u;
    BUTTON_PIO_IRQ_MASK = 0x3u;
}

/* ── Delay ────────────────────────────────────────────────────────────────── */

static void delay(volatile uint32_t count)
{
    WDT_CRR(WDT0_BASE) = WDT_KICK_VALUE;
    WDT_CRR(WDT1_BASE) = WDT_KICK_VALUE;
    while (count--)
        ;
}

/* ── Entry point ──────────────────────────────────────────────────────────── */

void __attribute__((noinline)) main(void)
{
    wdt_init();
    hps_bridge_init();
    gic_init();
    button_pio_init();

    /*
     * Enable IRQs.  From this point forward, button presses fire irq_c_handler.
     *
     * GDB: set a hardware breakpoint BEFORE this line to trace the sequence:
     *   (gdb) hbreak main
     *   (gdb) continue
     *   (gdb) next    # step through gic_init, button_pio_init, cpsie
     */
    __asm__ volatile("cpsie i" ::: "memory");

    while (1) {
        wdt_kick();
        /* Auto-advance pattern each iteration; ISR overrides led_pattern on button press. */
        uint32_t idx = g_debug.step_count % NUM_PATTERNS;
        g_debug.led_pattern = patterns[idx];
        LED_PIO_DATA = g_debug.led_pattern;
        g_debug.step_count++;
        delay(500000UL);
    }
}

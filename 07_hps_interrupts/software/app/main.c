/*
 * main.c — Bare-metal ARM interrupt demo for the DE10-Nano HPS.
 *
 * Handles push-button interrupts from KEY[0] and KEY[1] via the Cyclone V
 * Generic Interrupt Controller (GIC).  The button_pio peripheral on the
 * Lightweight HPS-to-FPGA bridge generates a level-high IRQ whenever a
 * falling edge is captured; the IRQ stays asserted until the ISR clears the
 * edge-capture register.
 *
 * Concepts demonstrated:
 *   - GIC Distributor and CPU Interface configuration (ARM GIC v1)
 *   - ARM exception modes (IRQ vs SVC), CPSR manipulation with cpsie/cpsid
 *   - FPGA-to-HPS interrupt routing (F2H_IRQ[0] → GIC SPI[40] → ID 72)
 *   - Volatile shared variables between interrupt context and main context
 *
 * Memory map (ARM perspective):
 *   H2F LW bridge base        : 0xFF200000
 *   LED PIO DATA               : 0xFF200000 (offset 0x0000)
 *   Button PIO DATA (read)     : 0xFF201000 (offset 0x1000)
 *   Button PIO IRQ_MASK (r/w)  : 0xFF201008 (offset 0x1008)
 *   Button PIO EDGE_CAP (r/w)  : 0xFF20100C (offset 0x100C)
 *
 * GIC base addresses (Cyclone V HPS TRM, section A.4):
 *   PERIPHBASE = 0xFFFEC000
 *   GIC CPU Interface (GICC) = PERIPHBASE + 0x0100 = 0xFFFEC100
 *   GIC Distributor  (GICD) = PERIPHBASE + 0x1000 = 0xFFFED000
 *
 * FPGA-to-HPS interrupt mapping:
 *   button_pio.irq → f2h_irq0[0] → GIC SPI[40] → interrupt ID 72
 */

#include <stdint.h>

/* ── Register addresses ───────────────────────────────────────────────────── */

/* Watchdog Timers — cannot be disabled; maximise timeout and kick regularly. */
#define WDT0_BASE           0xFFD02000UL
#define WDT1_BASE           0xFFD03000UL
#define WDT_TORR(b)         (*(volatile uint32_t *)((b) + 0x04UL))
#define WDT_CRR(b)          (*(volatile uint32_t *)((b) + 0x0CUL))
#define WDT_KICK_VALUE      0x76u

/* Cortex-A9 MPCore private watchdog */
#define MPCORE_WDT_BASE     0xFFFEC620UL
#define MPCORE_WDT_LOAD     (*(volatile uint32_t *)(MPCORE_WDT_BASE + 0x00UL))
#define MPCORE_WDT_CTRL     (*(volatile uint32_t *)(MPCORE_WDT_BASE + 0x08UL))
#define MPCORE_WDT_DISABLE  (*(volatile uint32_t *)(MPCORE_WDT_BASE + 0x14UL))

/* Reset Manager */
#define RSTMGR_BASE             0xFFD05000UL
#define RSTMGR_BRGMODRST        (*(volatile uint32_t *)(RSTMGR_BASE + 0x01CUL))
#define BRGMODRST_HPS2FPGA      (1u << 0)
#define BRGMODRST_LWHPS2FPGA    (1u << 1)
#define BRGMODRST_FPGA2HPS      (1u << 2)
#define BRGMODRST_ALL           (BRGMODRST_HPS2FPGA | BRGMODRST_LWHPS2FPGA | BRGMODRST_FPGA2HPS)

/* L3 (NIC-301) interconnect remap */
#define L3_REMAP                (*(volatile uint32_t *)0xFF800000UL)
#define L3_REMAP_OCRAM          (1u << 0)
#define L3_REMAP_LWHPS2FPGA     (1u << 4)

/* Lightweight HPS-to-FPGA bridge base */
#define H2F_LW_BASE             0xFF200000UL

/* LED PIO (8-bit output, at LW offset 0x0000) */
#define LED_PIO_DATA    (*(volatile uint32_t *)(H2F_LW_BASE + 0x0000UL))

/* Button PIO (2-bit input, at LW offset 0x1000).
 * altera_avalon_pio register map (for input PIO with edge capture):
 *   +0x00  DATA         — read current pin state
 *   +0x08  IRQ_MASK     — bit mask: 1 = enable interrupt for that input
 *   +0x0C  EDGE_CAPTURE — sticky: set on falling edge, cleared by writing 1s */
#define BUTTON_PIO_BASE     (H2F_LW_BASE + 0x1000UL)
#define BUTTON_PIO_DATA     (*(volatile uint32_t *)(BUTTON_PIO_BASE + 0x00UL))
#define BUTTON_PIO_IRQ_MASK (*(volatile uint32_t *)(BUTTON_PIO_BASE + 0x08UL))
#define BUTTON_PIO_EDGE_CAP (*(volatile uint32_t *)(BUTTON_PIO_BASE + 0x0CUL))

/* ── GIC registers (ARM Generic Interrupt Controller v1) ─────────────────── */
#define GICD_BASE   0xFFFED000UL
#define GICC_BASE   0xFFFEC100UL

/* Distributor registers */
#define GICD_CTLR           (*(volatile uint32_t *)(GICD_BASE + 0x000UL))
#define GICD_ISENABLER(n)   (*(volatile uint32_t *)(GICD_BASE + 0x100UL + 4u*(n)))
#define GICD_ICENABLER(n)   (*(volatile uint32_t *)(GICD_BASE + 0x180UL + 4u*(n)))
/* IPRIORITYR and ITARGETSR use byte-wide access (one byte per interrupt). */
#define GICD_IPRIORITYR(n)  (*(volatile uint8_t  *)(GICD_BASE + 0x400UL + (n)))
#define GICD_ITARGETSR(n)   (*(volatile uint8_t  *)(GICD_BASE + 0x800UL + (n)))

/* CPU Interface registers */
#define GICC_CTLR   (*(volatile uint32_t *)(GICC_BASE + 0x000UL))
#define GICC_PMR    (*(volatile uint32_t *)(GICC_BASE + 0x004UL))
#define GICC_IAR    (*(volatile uint32_t *)(GICC_BASE + 0x00CUL))
#define GICC_EOIR   (*(volatile uint32_t *)(GICC_BASE + 0x010UL))

/*
 * FPGA-to-HPS interrupt mapping:
 *   f2h_irq0[0] → GIC SPI[40] → GIC interrupt ID 72
 */
#define BUTTON_GIC_IRQ  72u

/* ── Shared state (written by IRQ handler, read by main loop) ──────────────── */

/* Current LED display pattern. */
static volatile uint8_t g_led_pattern = 0x01u;

/* Running count of KEY[1] presses. */
static volatile uint32_t g_press_count = 0u;

/* ── Forward declarations ─────────────────────────────────────────────────── */
static void wdt_init(void);
static void wdt_kick(void);
static void hps_bridge_init(void);
static void gic_init(void);
static void button_pio_init(void);
static void delay(volatile uint32_t count);

/* ── IRQ C handler (called from irq_entry in startup.S) ──────────────────── */

/*
 * irq_c_handler — C-level IRQ dispatcher.
 *
 * Read the GIC CPU Interface Interrupt Acknowledge Register (IAR) to:
 *   1. Identify the interrupt source (bits [9:0] = interrupt ID).
 *   2. Prevent the same interrupt from re-entering (clears the pending state
 *      in the CPU interface until EOIR is written).
 *
 * For the button_pio IRQ:
 *   - Read the edge-capture register to determine which buttons were pressed.
 *   - Clear the edge-capture register BEFORE updating shared state to avoid
 *     missing edges that arrive during the handler.
 *   - KEY[0]: rotate LED pattern left (barrel shift)
 *   - KEY[1]: increment counter; show on LEDs
 *
 * After handling, write to EOIR to signal end-of-interrupt to the GIC.
 * This allows the distributor to forward the next pending interrupt.
 */
void irq_c_handler(void)
{
    uint32_t iar    = GICC_IAR;
    uint32_t irq_id = iar & 0x3FFu;   /* bits [9:0]: interrupt ID */

    if (irq_id == BUTTON_GIC_IRQ) {
        /* Read and clear edge-capture register atomically. */
        uint32_t edges = BUTTON_PIO_EDGE_CAP;
        BUTTON_PIO_EDGE_CAP = edges;            /* write 1s to clear captured bits */

        if (edges & 0x1u) {
            /* KEY[0]: barrel-rotate LED pattern one position to the left. */
            g_led_pattern = (uint8_t)((g_led_pattern << 1u) | (g_led_pattern >> 7u));
        }

        if (edges & 0x2u) {
            /* KEY[1]: increment press counter; show lower 8 bits on LEDs. */
            g_press_count++;
            g_led_pattern = (uint8_t)g_press_count;
        }
    }

    /* Signal end-of-interrupt: restores the interrupt's active state in GIC. */
    GICC_EOIR = iar;
}

/* ── Watchdog ─────────────────────────────────────────────────────────────── */

static void wdt_init(void)
{
    WDT_TORR(WDT0_BASE) = 0xFFu;
    WDT_CRR(WDT0_BASE)  = WDT_KICK_VALUE;
    WDT_TORR(WDT1_BASE) = 0xFFu;
    WDT_CRR(WDT1_BASE)  = WDT_KICK_VALUE;

    /* Disable Cortex-A9 MPCore private watchdog via magic disable sequence. */
    MPCORE_WDT_DISABLE = 0x12345678u;
    MPCORE_WDT_DISABLE = 0x87654321u;
    MPCORE_WDT_CTRL    = 0u;
}

static inline void wdt_kick(void)
{
    WDT_CRR(WDT0_BASE) = WDT_KICK_VALUE;
    WDT_CRR(WDT1_BASE) = WDT_KICK_VALUE;
}

/* ── Bridge and L3 interconnect ───────────────────────────────────────────── */

static void hps_bridge_init(void)
{
    L3_REMAP = L3_REMAP_LWHPS2FPGA | L3_REMAP_OCRAM;

    RSTMGR_BRGMODRST |= BRGMODRST_ALL;
    delay(200000UL);
    RSTMGR_BRGMODRST &= ~BRGMODRST_ALL;
    delay(200000UL);
}

/* ── GIC Distributor and CPU Interface initialisation ─────────────────────── */

/*
 * gic_init — configure the ARM GIC to deliver button_pio interrupts to CPU 0.
 *
 * Configuration sequence (ARM GIC Architecture Specification, section 3.1):
 *   1. Disable the distributor while changing configuration.
 *   2. Set interrupt priority (lower value = higher priority; 0xA0 is mid-range).
 *   3. Set interrupt target CPU (bit 0 = CPU 0).
 *   4. Enable (unmask) the individual interrupt in the distributor.
 *   5. Re-enable the distributor.
 *   6. Set CPU interface priority mask (0xF0 passes all priorities ≤ 0xF0).
 *   7. Enable the CPU interface.
 *
 * Interrupt sensitivity: The altera_avalon_pio holds its IRQ line HIGH while
 * the edge-capture register is non-zero.  This is level-sensitive, which is
 * the GIC SPI default — no ICFGR change is needed.
 */
static void gic_init(void)
{
    /* 1. Disable distributor */
    GICD_CTLR = 0u;

    /* 2. Set priority for the button IRQ */
    GICD_IPRIORITYR(BUTTON_GIC_IRQ) = 0xA0u;

    /* 3. Route to CPU 0 (bit 0 = CPU 0) */
    GICD_ITARGETSR(BUTTON_GIC_IRQ) = 0x01u;

    /* 4. Enable the interrupt in the distributor set-enable register */
    GICD_ISENABLER(BUTTON_GIC_IRQ / 32u) = (1u << (BUTTON_GIC_IRQ % 32u));

    /* 5. Re-enable the distributor */
    GICD_CTLR = 1u;

    /* Memory barrier: ensure distributor writes complete before CPU interface */
    __asm__ volatile("dsb" ::: "memory");

    /* 6. Allow interrupts of priority 0xF0 and below */
    GICC_PMR = 0xF0u;

    /* 7. Enable the CPU interface */
    GICC_CTLR = 1u;
}

/* ── Button PIO initialisation ────────────────────────────────────────────── */

/*
 * button_pio_init — prepare the button PIO for interrupt-driven use.
 *
 * Clear any stale edges from before reset, then enable the IRQ mask for
 * both buttons.  The PIO will now assert its IRQ output whenever a falling
 * edge is captured on KEY[0] or KEY[1].
 */
static void button_pio_init(void)
{
    BUTTON_PIO_EDGE_CAP = 0x3u;   /* clear any stale edges */
    BUTTON_PIO_IRQ_MASK = 0x3u;   /* enable both buttons */
}

/* ── Delay with watchdog kick ─────────────────────────────────────────────── */

static void delay(volatile uint32_t count)
{
    WDT_CRR(WDT0_BASE) = WDT_KICK_VALUE;
    WDT_CRR(WDT1_BASE) = WDT_KICK_VALUE;
    while (count--)
        ;
}

/* ── Entry point ──────────────────────────────────────────────────────────── */

void main(void)
{
    wdt_init();
    hps_bridge_init();
    gic_init();
    button_pio_init();

    /*
     * Enable IRQ delivery to the CPU by clearing CPSR bit 7 (I flag).
     * From this point forward, button presses trigger irq_entry → irq_c_handler.
     */
    __asm__ volatile("cpsie i" ::: "memory");

    /* Main loop: display the current pattern and kick watchdogs periodically. */
    while (1) {
        wdt_kick();
        LED_PIO_DATA = g_led_pattern;
        delay(500000UL);
    }
}

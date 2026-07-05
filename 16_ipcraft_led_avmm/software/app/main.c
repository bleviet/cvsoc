#include <stdint.h>
#include "sys/alt_stdio.h"
#include "system.h"
#include "io.h"

/*
 * LED cycling + heartbeat-event demo for the IPCraft-generated
 * led_controller_avmm peripheral (cvsoc roadmap Phase 2.2, built with
 * IPCraft instead of hand-written VHDL -- see docs/tutorials/
 * led-controller-avmm-authoring.md in the ipcraft-vscode repo).
 *
 * Unlike the stock altera_avalon_pio used in 04_nios2_led, a hand-authored
 * Platform Designer component has no matching HAL register-access header
 * (no altera_avalon_pio_regs.h equivalent), so registers are poked directly
 * by byte offset via IOWR_32DIRECT/IORD_32DIRECT. Offsets match
 * led_controller_avmm.mm.yml:
 *   0x00 VERSION       (read-only)
 *   0x04 LED_PATTERN   (read-write)
 *   0x08 EVENTS        (read-write-1-to-clear: bit0 HEARTBEAT_ACTIVE,
 *                        bit1 HEARTBEAT_TOGGLED)
 *
 * printf is replaced with alt_printf (sys/alt_stdio.h) to avoid pulling in
 * newlib's full vfprintf chain (dtoa + mprec), which alone overflows the
 * 32 KB on-chip RAM. alt_printf supports %x, %s, %c only -- no zero-padding
 * or uppercase hex -- but is sufficient for diagnostic output here.
 */

#define LED_CTRL_REG_VERSION     0x00
#define LED_CTRL_REG_LED_PATTERN 0x04
#define LED_CTRL_REG_EVENTS      0x08
#define LED_CTRL_EVENTS_HEARTBEAT_TOGGLED (1u << 1)

#define EXPECTED_VERSION 0x00000100u /* MAJOR=1, MINOR=0 */

static void delay_ms(uint32_t ms)
{
    volatile uint32_t i, j;
    for (i = 0; i < ms; i++)
        for (j = 0; j < 5000; j++)
            ;
}

static const uint8_t patterns[] = {
    0x01, 0x03, 0x07, 0x0F, 0x1F, 0x3F, 0x7F, 0xFF,
    0x7F, 0x3F, 0x1F, 0x0F, 0x07, 0x03, 0x01, 0x00,
    0xAA, 0x55, 0xFF, 0x00,
};

int main(void)
{
    uint32_t idx = 0;
    const uint32_t num_patterns = sizeof(patterns) / sizeof(patterns[0]);

    /* Startup self-test: VERSION must match what IPCraft generated. */
    uint32_t version = IORD_32DIRECT(LED_CTRL_BASE, LED_CTRL_REG_VERSION);
    if (version != EXPECTED_VERSION) {
        alt_printf("FATAL: led_controller_avmm VERSION mismatch: expected 0x%x, got 0x%x\n",
               (unsigned)EXPECTED_VERSION, (unsigned)version);
    } else {
        alt_printf("led_controller_avmm VERSION OK: 0x%x\n", (unsigned)version);
    }

    while (1) {
        IOWR_32DIRECT(LED_CTRL_BASE, LED_CTRL_REG_LED_PATTERN, patterns[idx]);
        idx = (idx + 1) % num_patterns;

        uint32_t events = IORD_32DIRECT(LED_CTRL_BASE, LED_CTRL_REG_EVENTS);
        if (events & LED_CTRL_EVENTS_HEARTBEAT_TOGGLED) {
            alt_putstr("heartbeat\n");
            /* Write-1-to-clear: only touch the bit we observed set. */
            IOWR_32DIRECT(LED_CTRL_BASE, LED_CTRL_REG_EVENTS,
                          LED_CTRL_EVENTS_HEARTBEAT_TOGGLED);
        }

        delay_ms(200);
    }

    return 0;
}

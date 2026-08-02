#include <stdint.h>
#include "system.h"
#include "altera_avalon_pio_regs.h"

/*
 * LED cycling patterns for the DE10-Nano board.
 * Patterns are written to LED_PIO_BASE via Avalon-MM memory-mapped I/O.
 * The IOWR_ALTERA_AVALON_PIO_DATA macro is provided by the HAL BSP.
 */

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

    while (1) {
        IOWR_ALTERA_AVALON_PIO_DATA(LED_PIO_BASE, patterns[idx]);
        idx = (idx + 1) % num_patterns;
        delay_ms(200);
    }

    return 0;
}

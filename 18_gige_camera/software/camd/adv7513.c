/*
 * adv7513.c — ADV7513 configuration (see adv7513.h).
 */
#include <stddef.h>
#include <stdint.h>

#include "adv7513.h"

#define REG_POWER   0x41                /* bit 6: power down */
#define REG_STATUS  0x42                /* bit 6: hot-plug detect */
#define POWER_DOWN  0x40
#define HPD         0x40

/* Terasic HDMI_TX demo values, plus 0x17 = 16:9 for 720p. */
static const uint8_t init_table[][2] = {
    { 0x98, 0x03 },     /* must be 0x03 */
    { 0x01, 0x00 },     /* audio N = 6144 */
    { 0x02, 0x18 },
    { 0x03, 0x00 },
    { 0x14, 0x70 },     /* audio channel status: 8 channels */
    { 0x15, 0x20 },     /* input 4:4:4 (RGB) with separate syncs, 48 kHz */
    { 0x16, 0x30 },     /* output 4:4:4, 8 bits per channel */
    { 0x17, 0x02 },     /* aspect ratio 16:9 */
    { 0x18, 0x46 },     /* color space conversion off */
    { 0x40, 0x80 },     /* general control packet on */
    { 0x41, 0x10 },     /* power up */
    { 0x49, 0xA8 },     /* dither mode */
    { 0x55, 0x10 },     /* AVI infoframe: RGB */
    { 0x56, 0x08 },     /* AVI infoframe: active format = coded frame */
    { 0x96, 0xF6 },     /* clear interrupts */
    { 0x73, 0x07 },
    { 0x76, 0x1F },
    { 0x98, 0x03 },     /* fixed values from the programming guide */
    { 0x99, 0x02 },
    { 0x9A, 0xE0 },
    { 0x9C, 0x30 },
    { 0x9D, 0x61 },
    { 0xA2, 0xA4 },
    { 0xA3, 0xA4 },
    { 0xA5, 0x04 },
    { 0xAB, 0x40 },
    { 0xAF, 0x16 },     /* HDMI mode */
    { 0xBA, 0x60 },     /* no input clock delay */
    { 0xD1, 0xFF },
    { 0xDE, 0x10 },
    { 0xE4, 0x60 },
    { 0xFA, 0x7D },
};

static int wr(struct i2c_bus *b, uint8_t reg, uint8_t v)
{
    return i2c_write8(b, ADV7513_I2C_ADDR, reg, v);
}

int adv7513_init(struct i2c_bus *b)
{
    for (size_t i = 0; i < sizeof(init_table) / sizeof(init_table[0]); i++)
        if (wr(b, init_table[i][0], init_table[i][1]) < 0)
            return -1;
    return 0;
}

int adv7513_poll(struct i2c_bus *b, int *hpd)
{
    uint8_t status, power;

    if (i2c_read8(b, ADV7513_I2C_ADDR, REG_STATUS, &status) < 0 ||
        i2c_read8(b, ADV7513_I2C_ADDR, REG_POWER, &power) < 0)
        return -1;
    *hpd = !!(status & HPD);
    if (!(status & HPD) || !(power & POWER_DOWN))
        return 0;
    return adv7513_init(b) < 0 ? -1 : 1;
}

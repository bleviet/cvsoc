/*
 * i2c_bitbang.h — I2C master driven by toggling two open-drain lines.
 *
 * camd reaches the D5M's SCLK/SDATA through cam_frame_writer's I2C register
 * and the ADV7513's through hdmi_preview's (ADR-009); the bus code only
 * needs "pull low / release" and "read level".
 */
#ifndef I2C_BITBANG_H
#define I2C_BITBANG_H

#include <stdint.h>

struct i2c_bus {
    void *ctx;
    void (*drive)(void *ctx, int scl_low, int sda_low);
    int  (*sense)(void *ctx);           /* bit 0: SCL level, bit 1: SDA level */
    unsigned half_period_ns;            /* 5000 = 100 kHz */
};

/* Free a stuck bus: clock SCL until a slave releases SDA, then STOP. */
void i2c_recover(struct i2c_bus *bus);

/* 16-bit register access (8-bit register address, value MSB first), as used
 * by the MT9P001. Return 0, or -1 if the device did not acknowledge. */
int  i2c_write16(struct i2c_bus *bus, uint8_t addr7, uint8_t reg, uint16_t value);
int  i2c_read16(struct i2c_bus *bus, uint8_t addr7, uint8_t reg, uint16_t *value);
/* 8-bit register access, as used by the ADV7513. */
int  i2c_write8(struct i2c_bus *bus, uint8_t addr7, uint8_t reg, uint8_t value);
int  i2c_read8(struct i2c_bus *bus, uint8_t addr7, uint8_t reg, uint8_t *value);

#endif /* I2C_BITBANG_H */

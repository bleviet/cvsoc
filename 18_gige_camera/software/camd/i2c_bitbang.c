/*
 * i2c_bitbang.c — I2C master over two open-drain lines (see i2c_bitbang.h).
 *
 * Delays busy-wait on CLOCK_MONOTONIC: nanosleep() on the Cortex-A9 sleeps
 * tens of microseconds at minimum, which would slow a register write to
 * milliseconds. A whole sensor configuration takes a few milliseconds.
 */
#define _GNU_SOURCE
#include <time.h>

#include "i2c_bitbang.h"
#include "util.h"

#define SCL 1
#define SDA 2

static void delay(const struct i2c_bus *b)
{
    uint64_t until = now_ns() + b->half_period_ns;

    while (now_ns() < until)
        ;
}

/* Line levels: 1 releases the line (pulled high), 0 pulls it low. */
static void set(struct i2c_bus *b, int scl, int sda)
{
    b->drive(b->ctx, !scl, !sda);
}

/* Release SCL and wait for it to go high (a slave may stretch the clock). */
static int scl_high(struct i2c_bus *b, int sda)
{
    uint64_t deadline = now_ns() + 10000000ull;

    set(b, 1, sda);
    while (!(b->sense(b->ctx) & SCL)) {
        if (now_ns() > deadline)
            return -1;
    }
    delay(b);
    return 0;
}

static void start(struct i2c_bus *b)
{
    set(b, 1, 1);
    delay(b);
    set(b, 1, 0);
    delay(b);
    set(b, 0, 0);
    delay(b);
}

static void stop(struct i2c_bus *b)
{
    set(b, 0, 0);
    delay(b);
    scl_high(b, 0);
    set(b, 1, 1);
    delay(b);
}

/* Returns 0 if the byte was acknowledged. */
static int write_byte(struct i2c_bus *b, uint8_t v)
{
    int ack;

    for (int i = 7; i >= 0; i--) {
        int bit = (v >> i) & 1;

        set(b, 0, bit);
        delay(b);
        if (scl_high(b, bit) < 0)
            return -1;
        set(b, 0, bit);
    }
    set(b, 0, 1);                       /* release SDA for the ACK */
    delay(b);
    if (scl_high(b, 1) < 0)
        return -1;
    ack = !(b->sense(b->ctx) & SDA);
    set(b, 0, 1);
    delay(b);
    return ack ? 0 : -1;
}

static uint8_t read_byte(struct i2c_bus *b, int ack)
{
    uint8_t v = 0;

    for (int i = 7; i >= 0; i--) {
        set(b, 0, 1);
        delay(b);
        scl_high(b, 1);
        v |= (uint8_t)(!!(b->sense(b->ctx) & SDA) << i);
        set(b, 0, 1);
    }
    set(b, 0, !ack);
    delay(b);
    scl_high(b, !ack);
    set(b, 0, !ack);
    delay(b);
    return v;
}

void i2c_recover(struct i2c_bus *b)
{
    set(b, 1, 1);
    delay(b);
    for (int i = 0; i < 9 && !(b->sense(b->ctx) & SDA); i++) {
        set(b, 0, 1);
        delay(b);
        set(b, 1, 1);
        delay(b);
    }
    stop(b);
}

int i2c_write16(struct i2c_bus *b, uint8_t addr7, uint8_t reg, uint16_t value)
{
    int rc;

    start(b);
    rc = write_byte(b, (uint8_t)(addr7 << 1));
    if (rc == 0)
        rc = write_byte(b, reg);
    if (rc == 0)
        rc = write_byte(b, (uint8_t)(value >> 8));
    if (rc == 0)
        rc = write_byte(b, (uint8_t)value);
    stop(b);
    return rc;
}

int i2c_read16(struct i2c_bus *b, uint8_t addr7, uint8_t reg, uint16_t *value)
{
    int rc;

    start(b);
    rc = write_byte(b, (uint8_t)(addr7 << 1));
    if (rc == 0)
        rc = write_byte(b, reg);
    if (rc == 0) {
        /* Repeated START, then the read address. */
        set(b, 0, 1);
        delay(b);
        scl_high(b, 1);
        start(b);
        rc = write_byte(b, (uint8_t)(addr7 << 1 | 1));
    }
    if (rc == 0) {
        uint8_t hi = read_byte(b, 1);
        uint8_t lo = read_byte(b, 0);

        *value = (uint16_t)(hi << 8 | lo);
    }
    stop(b);
    return rc;
}

int i2c_write8(struct i2c_bus *b, uint8_t addr7, uint8_t reg, uint8_t value)
{
    int rc;

    start(b);
    rc = write_byte(b, (uint8_t)(addr7 << 1));
    if (rc == 0)
        rc = write_byte(b, reg);
    if (rc == 0)
        rc = write_byte(b, value);
    stop(b);
    return rc;
}

int i2c_read8(struct i2c_bus *b, uint8_t addr7, uint8_t reg, uint8_t *value)
{
    int rc;

    start(b);
    rc = write_byte(b, (uint8_t)(addr7 << 1));
    if (rc == 0)
        rc = write_byte(b, reg);
    if (rc == 0) {
        set(b, 0, 1);
        delay(b);
        scl_high(b, 1);
        start(b);
        rc = write_byte(b, (uint8_t)(addr7 << 1 | 1));
    }
    if (rc == 0)
        *value = read_byte(b, 0);
    stop(b);
    return rc;
}

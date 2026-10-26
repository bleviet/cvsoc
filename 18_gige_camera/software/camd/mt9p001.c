/*
 * mt9p001.c — MT9P001 timing model and configuration (see mt9p001.h).
 */
#include <math.h>

#include "mt9p001.h"

static double clampd(double v, double lo, double hi)
{
    return v < lo ? lo : v > hi ? hi : v;
}

/* Analog gain in 1/8 steps, with the 2x multiplier above 7.875. */
static uint16_t gain_code(double gain)
{
    gain = clampd(gain, 1.0, 15.75);
    if (gain <= 63 / 8.0)
        return (uint16_t)clampd(round(gain * 8), 8, 63);
    return (uint16_t)(1u << 6 | (unsigned)clampd(round(gain * 4), 8, 63));
}

double mt9p001_max_frame_rate(uint32_t width, uint32_t height, double pixclk_hz,
                              uint32_t hb_min)
{
    double lrow = (width + 2 * MT9P001_TRIM) / 2.0 + hb_min;

    return pixclk_hz / (2.0 * lrow * (height + 2 * MT9P001_TRIM + MT9P001_VB_MIN));
}

void mt9p001_compute(const struct mt9p001_mode *m, struct mt9p001_regs *r)
{
    double period = m->pixclk_hz / m->frame_rate;     /* frame time, pixclks */
    /* The sensor sends the trim pixels too (see MT9P001_TRIM). */
    uint32_t w2 = m->width / 2 + MT9P001_TRIM, h = m->height + 2 * MT9P001_TRIM;
    uint32_t lrow = w2 + m->hb_min, hb = 1, vb, sw;

    /* Shortest rows first; lengthen them only if the vertical blank alone
     * cannot stretch the frame far enough. */
    vb = (uint32_t)clampd(round(period / (2.0 * lrow) - h), MT9P001_VB_MIN, 1e9);
    if (vb > MT9P001_VB_MAX) {
        lrow = (uint32_t)ceil(period / (2.0 * (h + MT9P001_VB_MAX)));
        hb = lrow - w2;
        if (hb > MT9P001_HB_MAX)
            hb = MT9P001_HB_MAX;
        lrow = w2 + hb;
        vb = (uint32_t)clampd(round(period / (2.0 * lrow) - h),
                              MT9P001_VB_MIN, MT9P001_VB_MAX);
    }

    /* Shutter width in rows; at most one row less than the frame so the
     * exposure does not lengthen it (VBmin = SW - H + 1). */
    sw = (uint32_t)clampd(round((m->exposure_us * 1e-6 * m->pixclk_hz / 2.0 +
                                 MT9P001_SHUTTER_OVERHEAD) / lrow),
                          1, h + vb - 1);

    /* Mirrored readout: the last row and column of the window come first.
     * Offsets count from the top left of the upright image. The window is
     * the kept image shifted by one pixel towards higher rows and columns,
     * plus the trim; at full size, the kept image includes one border row
     * and column (rows and columns 1998 and 2608) instead of the first active
     * ones. */
    r->row_start     = (uint16_t)(MT9P001_ROW_START_ACTIVE +
                                  (MT9P001_ACTIVE_HEIGHT - m->offset_y - m->height));
    r->column_start  = (uint16_t)(MT9P001_COLUMN_START_ACTIVE +
                                  (MT9P001_ACTIVE_WIDTH - m->offset_x - m->width));
    r->row_size      = (uint16_t)(h - 1);
    r->column_size   = (uint16_t)(2 * w2 - 1);
    r->horz_blank    = (uint16_t)(hb - 1);
    r->vert_blank    = (uint16_t)(vb - 1);
    r->shutter_upper = (uint16_t)(sw >> 16);
    r->shutter_lower = (uint16_t)sw;
    r->gain_green    = gain_code(m->gain);
    r->gain_red      = gain_code(m->gain * m->wb_red);
    r->gain_blue     = gain_code(m->gain * m->wb_blue);
    r->line_pixclks  = 2 * lrow;
    r->frame_rate    = m->pixclk_hz / (2.0 * lrow * (h + vb));
    r->exposure_us   = (sw * (double)lrow - MT9P001_SHUTTER_OVERHEAD) * 2.0 /
                       m->pixclk_hz * 1e6;
}

/* ── I2C ─────────────────────────────────────────────────────────────────── */

static int wr(struct i2c_bus *b, uint8_t reg, uint16_t v)
{
    return i2c_write16(b, MT9P001_I2C_ADDR, reg, v);
}

static int write_gains(struct i2c_bus *b, const struct mt9p001_regs *r)
{
    return wr(b, MT9P001_GREEN1_GAIN, r->gain_green) | wr(b, MT9P001_GREEN2_GAIN, r->gain_green) |
           wr(b, MT9P001_RED_GAIN, r->gain_red) | wr(b, MT9P001_BLUE_GAIN, r->gain_blue);
}

int mt9p001_probe(struct i2c_bus *b, uint16_t *chip_version)
{
    i2c_recover(b);
    return i2c_read16(b, MT9P001_I2C_ADDR, MT9P001_CHIP_VERSION, chip_version);
}

int mt9p001_setup(struct i2c_bus *b)
{
    /* Slow DOUT edges (see MT9P001_OUTPUT_CONTROL_RUN), and launch data on
     * PIXCLK's falling edge so the FPGA samples mid-bit on the rising edge
     * (quartus/de10_nano.sdc). Everything else stays at the power-up
     * defaults: continuous ERS, no mirror, skip or binning. */
    return wr(b, MT9P001_OUTPUT_CONTROL, MT9P001_OUTPUT_CONTROL_RUN) |
           wr(b, MT9P001_PIXEL_CLOCK, MT9P001_INVERT_PIXEL_CLOCK) |
           wr(b, MT9P001_READ_MODE_2, MT9P001_READ_MODE_2_RUN) |
           wr(b, MT9P001_ROW_ADDR_MODE, 0) |
           wr(b, MT9P001_COL_ADDR_MODE, 0);
}

int mt9p001_apply(struct i2c_bus *b, const struct mt9p001_regs *r)
{
    int rc;

    /* Hold the changes and release them together at a frame boundary, then
     * restart so the next frame already uses them. */
    rc = wr(b, MT9P001_OUTPUT_CONTROL,
            MT9P001_OUTPUT_CONTROL_RUN | MT9P001_SYNCHRONIZE_CHANGES);
    rc |= wr(b, MT9P001_ROW_START, r->row_start);
    rc |= wr(b, MT9P001_COLUMN_START, r->column_start);
    rc |= wr(b, MT9P001_ROW_SIZE, r->row_size);
    rc |= wr(b, MT9P001_COLUMN_SIZE, r->column_size);
    rc |= wr(b, MT9P001_HORZ_BLANK, r->horz_blank);
    rc |= wr(b, MT9P001_VERT_BLANK, r->vert_blank);
    rc |= wr(b, MT9P001_SHUTTER_UPPER, r->shutter_upper);
    rc |= wr(b, MT9P001_SHUTTER_LOWER, r->shutter_lower);
    rc |= write_gains(b, r);
    rc |= wr(b, MT9P001_OUTPUT_CONTROL, MT9P001_OUTPUT_CONTROL_RUN);
    rc |= wr(b, MT9P001_RESTART, 1);
    return rc ? -1 : 0;
}

int mt9p001_set_exposure(struct i2c_bus *b, const struct mt9p001_regs *r)
{
    int rc;

    rc = wr(b, MT9P001_OUTPUT_CONTROL,
            MT9P001_OUTPUT_CONTROL_RUN | MT9P001_SYNCHRONIZE_CHANGES);
    rc |= wr(b, MT9P001_SHUTTER_UPPER, r->shutter_upper);
    rc |= wr(b, MT9P001_SHUTTER_LOWER, r->shutter_lower);
    rc |= write_gains(b, r);
    rc |= wr(b, MT9P001_OUTPUT_CONTROL, MT9P001_OUTPUT_CONTROL_RUN);
    return rc ? -1 : 0;
}

int mt9p001_color_field(struct i2c_bus *b, int on, uint16_t value)
{
    if (!on)
        return wr(b, MT9P001_TEST_PATTERN, 0) |
               wr(b, MT9P001_READ_MODE_2, MT9P001_READ_MODE_2_RUN) |
               wr(b, MT9P001_ROW_BLACK_OFFSET, MT9P001_ROW_BLACK_OFFSET_DEFAULT);
    return wr(b, MT9P001_READ_MODE_2, MT9P001_READ_MODE_2_RUN & ~MT9P001_READ_MODE_2_DEFAULT) |
           wr(b, MT9P001_ROW_BLACK_OFFSET, 0) |
           wr(b, MT9P001_TEST_GREEN, value) | wr(b, MT9P001_TEST_RED, value) |
           wr(b, MT9P001_TEST_BLUE, value) | wr(b, MT9P001_TEST_PATTERN, 0x0001);
}

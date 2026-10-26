/*
 * test_mt9p001.c — host unit test of the MT9P001 timing model (make check).
 *
 * Expected values follow from the datasheet equations in mt9p001.h.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include "mt9p001.h"

static int failures;

#define CHECK(cond) do { \
    if (!(cond)) { \
        fprintf(stderr, "%s:%d: FAIL %s\n", __FILE__, __LINE__, #cond); \
        failures++; \
    } \
} while (0)

static struct mt9p001_mode full_res(double fps, double pixclk)
{
    return (struct mt9p001_mode){
        .width = 2592, .height = 1944, .frame_rate = fps, .exposure_us = 10000,
        .gain = 1.0, .wb_red = 1.0, .wb_blue = 1.0, .pixclk_hz = pixclk, .hb_min = MT9P001_HBMIN_DEFAULT,
    };
}

int main(void)
{
    struct mt9p001_regs r;
    struct mt9p001_mode m;

    /* The sensor reads the kept image plus one pixel on every side
     * (MT9P001_TRIM): 2594 x 1946, LROW = 1297 + 450 = 1747. */
    /* 96 MHz full resolution: the datasheet's ~14 fps. */
    CHECK(fabs(mt9p001_max_frame_rate(2592, 1944, 96e6, 450) - 14.054) < 0.01);
    /* 25 MHz (XCLKIN = clk / 2, PLL off): ~3.66 fps. */
    CHECK(fabs(mt9p001_max_frame_rate(2592, 1944, 25e6, 450) - 3.6599) < 0.001);

    /* 3 Hz: shortest rows, VB = round(25e6 / 3 / 3494 - 1946) = 439. */
    m = full_res(3.0, 25e6);
    mt9p001_compute(&m, &r);
    CHECK(r.horz_blank == 0);
    CHECK(r.vert_blank == 438);
    CHECK(r.line_pixclks == 3494);
    CHECK(fabs(r.frame_rate - 3.0) < 0.002);

    /* 1 Hz needs more than VB_MAX rows: rows get longer instead. */
    m = full_res(1.0, 25e6);
    mt9p001_compute(&m, &r);
    CHECK(r.vert_blank <= MT9P001_VB_MAX - 1);
    CHECK(r.horz_blank > 0);
    CHECK(fabs(r.frame_rate - 1.0) < 0.001);

    /* Asking for more than the sensor can do gives its maximum. */
    m = full_res(60.0, 25e6);
    mt9p001_compute(&m, &r);
    CHECK(r.vert_blank == MT9P001_VB_MIN - 1);
    CHECK(fabs(r.frame_rate - mt9p001_max_frame_rate(2592, 1944, 25e6, 450)) < 1e-9);

    /* Exposure: SW = round((10 ms * 12.5 MHz + 398) / 1747) = 72 rows,
     * tEXP = (72 * 1747 - 398) * 2 / 25 MHz = 10030.88 us. */
    m = full_res(3.0, 25e6);
    mt9p001_compute(&m, &r);
    CHECK(r.shutter_upper == 0 && r.shutter_lower == 72);
    CHECK(fabs(r.exposure_us - 10030.88) < 0.01);

    /* An exposure longer than the frame is clamped to H + VB - 1 rows. */
    m = full_res(3.0, 25e6);
    m.exposure_us = 1e6;
    mt9p001_compute(&m, &r);
    CHECK(r.shutter_lower == 1946 + 439 - 1);
    CHECK(fabs(r.frame_rate - 3.0) < 0.002);

    /* Full frame, mirrored, with the trim: rows 54..1999 and columns
     * 16..2609 (even starts, odd sizes as the sensor requires). */
    m = full_res(3.0, 25e6);
    mt9p001_compute(&m, &r);
    CHECK(r.row_start == 54 && r.column_start == 16);
    CHECK(r.row_size == 1945 && r.column_size == 2593);

    /* ROI: offsets count from the top left of the upright image, so the
     * window comes from the opposite corner of the array. */
    m = full_res(3.0, 25e6);
    m.width = 640;
    m.height = 480;
    m.offset_x = 976;
    m.offset_y = 732;
    mt9p001_compute(&m, &r);
    CHECK(r.column_start == 16 + 2592 - 976 - 640 && r.row_start == 54 + 1944 - 732 - 480);
    CHECK(r.column_size == 641 && r.row_size == 481);

    /* Gain: 1/8 steps up to 7.875, then the 2x multiplier. */
    m = full_res(3.0, 25e6);
    m.gain = 2.0;
    mt9p001_compute(&m, &r);
    CHECK(r.gain_green == 16);
    m.gain = 10.0;
    mt9p001_compute(&m, &r);
    CHECK(r.gain_green == (1 << 6 | 40));

    /* White balance: red and blue gains relative to green. */
    m.gain = 2.0;
    m.wb_red = 1.5;
    m.wb_blue = 0.25;
    mt9p001_compute(&m, &r);
    CHECK(r.gain_green == 16 && r.gain_red == 24 && r.gain_blue == 8);   /* blue: 1.0 minimum */

    if (failures) {
        fprintf(stderr, "test_mt9p001: %d failure(s)\n", failures);
        return EXIT_FAILURE;
    }
    printf("test_mt9p001: OK\n");
    return EXIT_SUCCESS;
}

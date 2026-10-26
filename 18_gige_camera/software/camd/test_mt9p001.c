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
        .gain = 1.0, .pixclk_hz = pixclk, .hb_min = MT9P001_HBMIN_DEFAULT,
    };
}

int main(void)
{
    struct mt9p001_regs r;
    struct mt9p001_mode m;

    /* 96 MHz full resolution: the datasheet's ~14 fps. */
    CHECK(fabs(mt9p001_max_frame_rate(2592, 1944, 96e6, 450) - 14.077) < 0.01);
    /* 25 MHz (XCLKIN = clk / 2, PLL off): ~3.67 fps. */
    CHECK(fabs(mt9p001_max_frame_rate(2592, 1944, 25e6, 450) - 3.6656) < 0.001);

    /* 3 Hz: shortest rows, VB = round(25e6 / 3 / 3492 - 1944) = 442. */
    m = full_res(3.0, 25e6);
    mt9p001_compute(&m, &r);
    CHECK(r.horz_blank == 0);
    CHECK(r.vert_blank == 441);
    CHECK(r.line_pixclks == 3492);
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

    /* Exposure: SW = round((10 ms * 12.5 MHz + 398) / 1746) = 72 rows,
     * tEXP = (72 * 1746 - 398) * 2 / 25 MHz = 10025.12 us. */
    m = full_res(3.0, 25e6);
    mt9p001_compute(&m, &r);
    CHECK(r.shutter_upper == 0 && r.shutter_lower == 72);
    CHECK(fabs(r.exposure_us - 10025.12) < 0.01);

    /* An exposure longer than the frame is clamped to H + VB - 1 rows. */
    m = full_res(3.0, 25e6);
    m.exposure_us = 1e6;
    mt9p001_compute(&m, &r);
    CHECK(r.shutter_lower == 1944 + 442 - 1);
    CHECK(fabs(r.frame_rate - 3.0) < 0.002);

    /* ROI: window registers in active-image coordinates. */
    m = full_res(3.0, 25e6);
    m.width = 640;
    m.height = 480;
    m.offset_x = 976;
    m.offset_y = 732;
    mt9p001_compute(&m, &r);
    CHECK(r.column_start == 16 + 976 && r.row_start == 54 + 732);
    CHECK(r.column_size == 639 && r.row_size == 479);

    /* Gain: 1/8 steps up to 7.875, then the 2x multiplier. */
    m = full_res(3.0, 25e6);
    m.gain = 2.0;
    mt9p001_compute(&m, &r);
    CHECK(r.gain == 16);
    m.gain = 10.0;
    mt9p001_compute(&m, &r);
    CHECK(r.gain == (1 << 6 | 40));

    if (failures) {
        fprintf(stderr, "test_mt9p001: %d failure(s)\n", failures);
        return EXIT_FAILURE;
    }
    printf("test_mt9p001: OK\n");
    return EXIT_SUCCESS;
}

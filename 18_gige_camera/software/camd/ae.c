/*
 * ae.c — auto exposure (see ae.h).
 */
#include <math.h>
#include <stddef.h>

#include "ae.h"

#define AE_DEAD_BAND    0.08            /* no change within +-8 % of the target */
#define AE_STEP_MAX     8.0             /* exposure x gain changes at most 8x per step */
#define AE_CLIP_FRACTION 0.25           /* more clipped cells: the mean underestimates */
#define AWB_DEAD_BAND   0.04            /* gain steps are 1/8 (3-12 %): don't hunt */
#define AWB_MIN_SIGNAL  4.0             /* channel means closer to black are noise */
#define AWB_WB_MIN      0.25
#define AWB_WB_MAX      4.0

static double clampd(double v, double lo, double hi)
{
    return v < lo ? lo : v > hi ? hi : v;
}

void ae_measure(const volatile uint8_t *frame, uint32_t width, uint32_t height,
                uint32_t max_cells, struct ae_stats *s)
{
    uint32_t cols = width / 2, rows = height / 2, step = 1, clipped = 0, n = 0;
    uint64_t sum = 0, g = 0, r = 0, bl = 0;

    /* Same step in x and y, so the grid covers the frame evenly. */
    while (max_cells && (uint64_t)(cols / step) * (rows / step) > max_cells)
        step++;

    for (uint32_t cy = step / 2; cy < rows; cy += step) {
        const volatile uint8_t *r0 = frame + (size_t)(2 * cy) * width;
        const volatile uint8_t *r1 = r0 + width;

        for (uint32_t cx = step / 2; cx < cols; cx += step) {
            uint8_t a = r0[2 * cx], b = r0[2 * cx + 1], c = r1[2 * cx], d = r1[2 * cx + 1];

            sum += (uint32_t)a + b + c + d;
            if (a >= AE_CLIP_LEVEL || b >= AE_CLIP_LEVEL ||
                c >= AE_CLIP_LEVEL || d >= AE_CLIP_LEVEL) {
                clipped++;
            } else {                    /* G1 R / B G2 */
                g += (uint32_t)a + d;
                r += b;
                bl += c;
            }
            n++;
        }
    }
    s->cells = n;
    s->mean = n ? sum / (4.0 * n) : 0.0;
    s->clipped = n ? (double)clipped / n : 0.0;
    s->green = n > clipped ? g / (2.0 * (n - clipped)) : 0.0;
    s->red = n > clipped ? (double)r / (n - clipped) : 0.0;
    s->blue = n > clipped ? (double)bl / (n - clipped) : 0.0;
}

/* Scale one channel's gain so its mean above black matches green's. */
static int awb_channel(double green, double mean, double *wb)
{
    double ratio, v;

    if (mean - AE_BLACK_LEVEL < AWB_MIN_SIGNAL || green - AE_BLACK_LEVEL < AWB_MIN_SIGNAL)
        return 0;
    ratio = (green - AE_BLACK_LEVEL) / (mean - AE_BLACK_LEVEL);
    if (fabs(ratio - 1.0) <= AWB_DEAD_BAND)
        return 0;
    v = clampd(*wb * ratio, AWB_WB_MIN, AWB_WB_MAX);
    if (fabs(v / *wb - 1.0) < 0.01)
        return 0;
    *wb = v;
    return 1;
}

int awb_step(const struct ae_stats *s, double *wb_red, double *wb_blue)
{
    int changed = awb_channel(s->green, s->red, wb_red);

    return awb_channel(s->green, s->blue, wb_blue) | changed;
}

static uint32_t cell(const volatile uint8_t *frame, uint32_t width, uint32_t cx, uint32_t cy)
{
    const volatile uint8_t *p = frame + (size_t)(2 * cy) * width + 2 * cx;

    return (uint32_t)p[0] + p[1] + p[width] + p[width + 1];
}

double ae_focus(const volatile uint8_t *frame, uint32_t width, uint32_t height,
                uint32_t max_cells)
{
    uint32_t cols = width / 2, rows = height / 2, step = 1;
    uint32_t x0 = cols / 4, x1 = cols - cols / 4 - 1, y0 = rows / 4, y1 = rows - rows / 4 - 1;
    uint64_t sq = 0, sum = 0, n = 0;

    while (max_cells && (uint64_t)((x1 - x0) / step) * ((y1 - y0) / step) > max_cells)
        step++;
    for (uint32_t cy = y0; cy < y1; cy += step)
        for (uint32_t cx = x0; cx < x1; cx += step) {
            uint32_t c = cell(frame, width, cx, cy);
            uint32_t r = cell(frame, width, cx + 1, cy), d = cell(frame, width, cx, cy + 1);

            sq += (uint64_t)((int64_t)c - r) * ((int64_t)c - r) +
                  (uint64_t)((int64_t)c - d) * ((int64_t)c - d);
            sum += c;
            n++;
        }
    return sum ? 100.0 * sqrt((double)sq / (2.0 * n)) / ((double)sum / n) : 0.0;
}

int ae_step(const struct ae_limits *l, const struct ae_stats *s, struct ae_setting *set)
{
    double signal = s->mean - AE_BLACK_LEVEL;
    double want = l->target - AE_BLACK_LEVEL;
    double ratio, total, exposure, gain;

    if (signal < 0.5)
        signal = 0.5;                   /* black: step up by the maximum */
    ratio = want / signal;
    if (fabs(ratio - 1.0) <= AE_DEAD_BAND)
        return 0;
    /* Clipping hides how far over the target a bright frame is: step down at
     * least 2x while much of it clips. A few clipped highlights (a lamp, a
     * window) are normal at the right exposure and do not count. */
    if (ratio < 1.0 && s->clipped > AE_CLIP_FRACTION && ratio > 0.5)
        ratio = 0.5;
    ratio = clampd(ratio, 1.0 / AE_STEP_MAX, AE_STEP_MAX);

    /* Exposure first (no noise cost), gain once exposure is at its limit. */
    total = set->exposure_us * set->gain * ratio;
    exposure = clampd(total / l->gain_min, l->exposure_min_us, l->exposure_max_us);
    gain = clampd(total / exposure, l->gain_min, l->gain_max);

    if (fabs(exposure / set->exposure_us - 1.0) < 0.01 && fabs(gain / set->gain - 1.0) < 0.01)
        return 0;                       /* at a limit */
    set->exposure_us = exposure;
    set->gain = gain;
    return 1;
}

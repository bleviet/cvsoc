/*
 * test_ae.c — host unit test of the auto exposure (make check).
 *
 * The convergence tests close the loop with a model of the sensor: a linear
 * response above the black level, clipped at 255, with the scene brightness
 * measured on the board (2026-10-26: mean 48.5 at 100 ms x gain 4).
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ae.h"

static int failures;

#define CHECK(cond) do { \
    if (!(cond)) { \
        fprintf(stderr, "%s:%d: FAIL %s\n", __FILE__, __LINE__, #cond); \
        failures++; \
    } \
} while (0)

#define W 64
#define H 48

static uint8_t frame[W * H];

static double highlight;                /* brightness of the top 6 rows, 0: none */

/* A scene: a horizontal ramp of reflectance 0.25..1.75 (mean 1), optionally
 * with a band of `highlight` (a lamp) across the top 12.5 %. */
static void render(double brightness)
{
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            double r = highlight && y < H / 8 ? highlight : 0.25 + 1.5 * x / (W - 1);
            double v = AE_BLACK_LEVEL + brightness * r;

            frame[y * W + x] = (uint8_t)(v > 255 ? 255 : v + 0.5);
        }
}

/* Mean above black per (us x gain) for the lab scene. */
#define LAB_RESPONSE ((48.5 - AE_BLACK_LEVEL) / (100000.0 * 4))

static const struct ae_limits limits = {
    .exposure_min_us = 100, .exposure_max_us = 272000,
    .gain_min = 1.0, .gain_max = 15.75, .target = AE_TARGET_DEFAULT,
};

/* Run the loop from `set` for a scene; returns the steps until it settled
 * (or 99), leaves the final mean in *mean. */
static int converge(double response, struct ae_setting *set, double *mean)
{
    struct ae_stats s;

    for (int i = 0; i < 99; i++) {
        render(set->exposure_us * set->gain * response);
        ae_measure(frame, W, H, 0, &s);
        *mean = s.mean;
        if (!ae_step(&limits, &s, set))
            return i;
    }
    return 99;
}

int main(void)
{
    struct ae_stats s;
    struct ae_setting set;
    double mean;
    int steps;

    /* Statistics: a flat frame, and one with a clipped quarter. */
    memset(frame, 100, sizeof(frame));
    ae_measure(frame, W, H, 0, &s);
    CHECK(s.cells == (W / 2) * (H / 2));
    CHECK(s.mean == 100.0 && s.clipped == 0.0);
    memset(frame, 255, W * H / 4);
    ae_measure(frame, W, H, 0, &s);
    CHECK(fabs(s.clipped - 0.25) < 1e-9);
    /* Subsampling keeps the cell count within the budget. */
    ae_measure(frame, W, H, 100, &s);
    CHECK(s.cells <= 100 && s.cells >= 50);

    /* Dead band: a frame at the target changes nothing. */
    set = (struct ae_setting){ 20000, 2.0 };
    s = (struct ae_stats){ .mean = AE_TARGET_DEFAULT + 3 };
    CHECK(ae_step(&limits, &s, &set) == 0);

    /* Too dark: exposure rises before gain, and by at most 8x per step. */
    set = (struct ae_setting){ 10000, 1.0 };
    s = (struct ae_stats){ .mean = AE_BLACK_LEVEL };
    CHECK(ae_step(&limits, &s, &set) == 1);
    CHECK(fabs(set.exposure_us - 80000) < 1e-6 && set.gain == 1.0);

    /* Much of the frame clipped: halve at least, though the mean is only a
     * little high. A few clipped highlights at the target change nothing. */
    set = (struct ae_setting){ 100000, 1.0 };
    s = (struct ae_stats){ .mean = AE_TARGET_DEFAULT + 20, .clipped = 0.3 };
    CHECK(ae_step(&limits, &s, &set) == 1);
    CHECK(fabs(set.exposure_us - 50000) < 1e-6);
    s = (struct ae_stats){ .mean = AE_TARGET_DEFAULT, .clipped = 0.1 };
    CHECK(ae_step(&limits, &s, &set) == 0);

    /* The lab: dim, needs the full frame time and some gain. Starts from
     * camd's old default (mean 14). */
    set = (struct ae_setting){ 20000, 2.0 };
    steps = converge(LAB_RESPONSE, &set, &mean);
    CHECK(steps < 6);
    CHECK(fabs(mean - AE_TARGET_DEFAULT) < 10);
    CHECK(set.exposure_us == limits.exposure_max_us && set.gain > 2.0 && set.gain < 6.0);
    printf("lab scene: %d steps, exposure %.0f us, gain %.2f, mean %.1f\n",
           steps, set.exposure_us, set.gain, mean);

    /* Daylight, 1000x brighter, from the lab setting: saturated at first,
     * then down to a short exposure at gain 1. */
    steps = converge(LAB_RESPONSE * 1000, &set, &mean);
    CHECK(steps < 10);
    CHECK(fabs(mean - AE_TARGET_DEFAULT) < 10);
    CHECK(set.gain == 1.0 && set.exposure_us < 2000);
    printf("daylight:  %d steps, exposure %.0f us, gain %.2f, mean %.1f\n",
           steps, set.exposure_us, set.gain, mean);

    /* The lab with a lamp in view (as on the board): settles with the lamp
     * clipped instead of hunting between the lamp and the mean. */
    highlight = 20.0;
    set = (struct ae_setting){ 20000, 2.0 };
    steps = converge(LAB_RESPONSE, &set, &mean);
    CHECK(steps < 8);
    CHECK(fabs(mean - AE_TARGET_DEFAULT) < 10);
    printf("with lamp: %d steps, exposure %.0f us, gain %.2f, mean %.1f\n",
           steps, set.exposure_us, set.gain, mean);
    highlight = 0.0;

    /* Darkness: stops at the limits instead of hunting. */
    steps = converge(0.0, &set, &mean);
    CHECK(steps < 6);
    CHECK(set.exposure_us == limits.exposure_max_us && set.gain == limits.gain_max);

    if (failures) {
        fprintf(stderr, "test_ae: %d failure(s)\n", failures);
        return EXIT_FAILURE;
    }
    printf("test_ae: OK\n");
    return EXIT_SUCCESS;
}

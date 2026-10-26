/*
 * ae.h — auto exposure: brightness statistics of a frame and the controller
 * that turns them into the next exposure time and gain.
 *
 * The controller aims the frame mean (above the sensor's black level) at a
 * target. The sensor is linear, so brightness above black is proportional to
 * exposure x gain: one step scales that product by the ratio target / mean,
 * bounded so a saturated or black frame converges in a few steps. Exposure is
 * raised first, up to the frame time; gain (noise) only after that.
 *
 * Pure functions, unit-tested on the host (test_ae.c); source_fpga.c runs
 * them on the latest frame in DDR.
 */
#ifndef AE_H
#define AE_H

#include <stdint.h>

#define AE_TARGET_DEFAULT   110.0       /* 8-bit mean: mid-grey on a monitor */
#define AE_BLACK_LEVEL      10.0        /* 8-bit black (MT9P001 Row_Black_Target 168 / 16) */
#define AE_CLIP_LEVEL       250         /* a 2x2 cell with a value at or above is clipped */

struct ae_stats {
    double mean;                        /* mean of all sampled pixels, 0..255 */
    double clipped;                     /* fraction of sampled 2x2 cells with a clipped pixel */
    uint32_t cells;                     /* 2x2 cells sampled */
};

struct ae_limits {
    double exposure_min_us, exposure_max_us;
    double gain_min, gain_max;
    double target;                      /* desired mean, 8-bit */
};

struct ae_setting {
    double exposure_us, gain;
};

/* Sample about `max_cells` 2x2 cells on a regular grid of a width x height
 * 8-bit frame (Bayer or mono; even sizes). Reads bytes one at a time, so the
 * frame may be in uncached memory. */
void ae_measure(const volatile uint8_t *frame, uint32_t width, uint32_t height,
                uint32_t max_cells, struct ae_stats *s);

/* One controller step: update *set from the statistics of a frame taken with
 * *set. Returns 1 if the setting changed (by more than the dead band), 0 if
 * the frame is close enough to the target or a limit stops the change. */
int ae_step(const struct ae_limits *l, const struct ae_stats *s, struct ae_setting *set);

#endif /* AE_H */

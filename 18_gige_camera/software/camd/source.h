/*
 * source.h — where camd's frames come from.
 *
 * sim:  frames are generated in software (no FPGA needed).
 * fpga: frames are written to reserved DDR by the FPGA test-pattern
 *       generator (hdl/cam_tpg_writer.vhd), later the sensor pipeline.
 */
#ifndef SOURCE_H
#define SOURCE_H

#include <stdint.h>

#include "camera.h"

struct frame_source {
    const char *name;
    /*
     * 1: the streamer paces frames by AcquisitionFrameRate (sim).
     * 0: next() blocks until the hardware has produced a frame.
     */
    int paced;
    /* Return width*height bytes for frame `frame_no`, or NULL on error. */
    const uint8_t *(*next)(struct frame_source *src, const struct image_cfg *img,
                           uint32_t frame_no);
    /* Optional: acquisition stopped (e.g. stop the hardware writer). */
    void (*idle)(struct frame_source *src);
    void (*close)(struct frame_source *src);
    void *priv;
};

struct frame_source *source_sim_open(uint32_t sensor_width, uint32_t sensor_height);
struct frame_source *source_fpga_open(uint32_t sensor_width, uint32_t sensor_height,
                                      uint32_t fb_base, uint32_t fb_size);

/*
 * Test-pattern definition shared by the sim source, the FPGA generator and
 * the Python model (client/tpg_model.py). (x, y) are sensor coordinates,
 * so an ROI shows a crop of the same pattern.
 *
 *   Off                       0
 *   GreyHorizontalRampMoving  (x + n) mod 256
 *   ColorBar                  8 vertical 100 % bars (white, yellow, cyan,
 *                             green, magenta, red, blue, black), bar index
 *                             x * 8 / sensor_width. BayerGR8 emits the CFA
 *                             sample (row even: G R, row odd: B G); Mono8
 *                             emits Y = (77 R + 150 G + 29 B) >> 8.
 */
uint8_t tpg_pixel(uint32_t pattern, uint32_t pixel_format, uint32_t x, uint32_t y,
                  uint32_t n, uint32_t sensor_width);

#endif /* SOURCE_H */

/*
 * source.h — where camd's frames come from.
 *
 * sim:  frames are generated in software (no FPGA needed).
 * fpga: frames are written to reserved DDR by the FPGA
 *       (hdl/cam_frame_writer.vhd): from the D5M sensor when TestPattern is
 *       Off, otherwise from its test-pattern generator.
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
    /* Optional, replaces idle: no client streams, keep producing frames with
     * `img` for a local display (HDMI). Called about every 100 ms. */
    void (*preview)(struct frame_source *src, const struct image_cfg *img);
    /* Optional: housekeeping, called about once a second from the main loop
     * (e.g. reconfigure the HDMI transmitter after a hot-plug). */
    void (*poll)(struct frame_source *src);
    /* Optional: highest AcquisitionFrameRate `img` allows (default
     * FRAME_RATE_MAX). Cheap and lock-free: called under the camera lock. */
    float (*max_frame_rate)(struct frame_source *src, const struct image_cfg *img);
    /* Optional: sensor frames lost to FIFO overflows and dropped for a size
     * mismatch, since camd started. */
    void (*counters)(struct frame_source *src, uint32_t *overflows, uint32_t *bad_frames);
    void (*close)(struct frame_source *src);
    int has_sensor;                     /* TestPattern Off shows a real image */
    void *priv;
};

/* Sensor settings from the command line until M3 makes them GenICam
 * features. With auto_exposure, exposure_us and gain are only the start
 * values (see ae.h). */
struct sensor_opts {
    double exposure_us;
    double gain;
    int    auto_exposure;
    double ae_target;                   /* desired 8-bit frame mean */
    unsigned hdmi_mode;                 /* hdmi_preview CTRL bits: 2 grey, 4 bars */
};

struct frame_source *source_sim_open(uint32_t sensor_width, uint32_t sensor_height);
struct frame_source *source_fpga_open(uint32_t sensor_width, uint32_t sensor_height,
                                      uint32_t fb_base, uint32_t fb_size,
                                      const struct sensor_opts *opts);
/* Bring-up check of the D5M data lines with the sensor's color-field test
 * pattern (camd --sensor-test). Returns 0 if every line works. */
int source_fpga_sensor_test(void);
/* Bring-up: read ("0x49") or write ("0x49=0x00a8") sensor registers in
 * order, then print the capture statistics (camd --sensor-reg). */
int source_fpga_sensor_regs(char **specs, int n);

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

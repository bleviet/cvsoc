/*
 * source_fpga.c — frames written into reserved DDR by cam_frame_writer (see
 * hdl/cam_frame_writer.vhd for the CSR map).
 *
 * The FPGA fills a ring of buffers in memory that Linux does not manage
 * (boot with mem=960M, buffers at 0x3C000000). camd waits for FRAME_COUNT to
 * advance and streams the buffer STATUS names as last completed. The mapping
 * is uncached (/dev/mem above the mem= limit), which ADR-001 measured as
 * fast enough for M1.
 *
 * With TestPattern Off the frames come from the D5M. camd configures the
 * MT9P001 over I2C, bit-banged through the writer's I2C register (ADR-009):
 * window = ROI, blanking = AcquisitionFrameRate, shutter = the fixed
 * exposure from the command line. PIXCLK is XCLKIN = 25 MHz (sensor PLL off).
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

#include "mt9p001.h"
#include "source.h"
#include "util.h"

#define LW_BRIDGE_BASE   0xFF200000u
#define FPGAMGR_BASE     0xFF706000u
#define RSTMGR_PAGE      0xFFD05000u
#define BRGMODRST_OFF    0x01Cu
#define BRGMODRST_F2H    (1u << 2)
#define FPGAMGR_USER_MODE 0x4u

#define FABRIC_CLK_HZ    50000000u
#define PIXCLK_HZ        25e6            /* XCLKIN = fabric clock / 2 */
#define CAM_ID           0x43564332u     /* "CVC2": M2 bitstream */
#define CAM_ID_M1        0x43564331u
#define FRAME_TIMEOUT_NS 2000000000ull

/* cam_frame_writer CSRs (word offsets) */
enum {
    CSR_ID, CSR_CTRL, CSR_STATUS, CSR_FRAME_COUNT, CSR_WIDTH, CSR_HEIGHT,
    CSR_OFFSET_X, CSR_OFFSET_Y, CSR_PATTERN, CSR_FORMAT, CSR_FRAME_INTERVAL,
    CSR_BUF_BASE, CSR_BUF_STRIDE, CSR_BUF_COUNT, CSR_SENSOR_WIDTH, CSR_FRAME_CYCLES,
    CSR_SENSOR_CTRL, CSR_I2C, CSR_CAP_FRAMES, CSR_CAP_SIZE, CSR_CAP_LINE_PERIOD,
    CSR_CAP_BITS, CSR_CAP_FLAGS, CSR_CAP_OVERFLOWS, CSR_FRAME_PERIOD, CSR_BAD_FRAMES,
};

#define SENSOR_RESET_N  (1u << 0)
#define SENSOR_XCLK     (1u << 1)
#define SENSOR_TRIGGER  (1u << 2)
#define SENSOR_CAPTURE  (1u << 3)

struct fpga {
    volatile uint32_t *csr;
    uint8_t  *fb;
    uint32_t  fb_base, fb_size, stride, nbufs;
    struct image_cfg applied;
    int       running;
    uint32_t  last_count;
    uint64_t  overruns;
    uint64_t  timeout_ns;

    /* Sensor */
    struct i2c_bus i2c;
    int       sensor;                   /* MT9P001 answered on I2C */
    int       warned_no_sensor;
    uint32_t  sensor_width, sensor_height;
    uint32_t  hb_min;
    struct sensor_opts opts;
    uint32_t  overflows0, bad0;         /* counters when camd started */
};

static void *map_phys(uint32_t addr, size_t len)
{
    int fd = open("/dev/mem", O_RDWR | O_SYNC);
    void *p;

    if (fd < 0) {
        perror("camd: /dev/mem");
        return NULL;
    }
    p = mmap(NULL, len, PROT_READ | PROT_WRITE, MAP_SHARED, fd, (off_t)addr);
    close(fd);
    if (p == MAP_FAILED) {
        fprintf(stderr, "camd: mmap 0x%08x: %s\n", addr, strerror(errno));
        return NULL;
    }
    return p;
}

/* Refuse to let the FPGA write into memory Linux is using. */
static int region_is_free(uint32_t base, uint32_t size)
{
    FILE *f = fopen("/proc/iomem", "r");
    char line[256];
    int ok = 1;

    if (!f) {
        perror("camd: /proc/iomem");
        return 0;
    }
    while (fgets(line, sizeof(line), f)) {
        uint64_t lo, hi;

        if (strstr(line, "System RAM") && sscanf(line, "%" SCNx64 "-%" SCNx64, &lo, &hi) == 2 &&
            lo < (uint64_t)base + size && hi >= base) {
            fprintf(stderr, "camd: frame buffers 0x%08x+0x%x overlap Linux RAM "
                    "(%08" PRIx64 "-%08" PRIx64 "); boot with mem=960M\n",
                    base, size, lo, hi);
            ok = 0;
        }
    }
    fclose(f);
    return ok;
}

static int fpga_ready(void)
{
    volatile uint32_t *mgr = map_phys(FPGAMGR_BASE, 4096);
    uint32_t stat;

    if (!mgr)
        return 0;
    stat = mgr[0];
    munmap((void *)mgr, 4096);
    if ((stat & 0x7) != FPGAMGR_USER_MODE) {
        fprintf(stderr, "camd: FPGA not configured (FPGA Manager state %u)\n", stat & 0x7);
        return 0;
    }
    return 1;
}

static void release_f2h_bridge(void)
{
    volatile uint32_t *rst = map_phys(RSTMGR_PAGE, 4096);

    if (!rst)
        return;
    rst[BRGMODRST_OFF / 4] &= ~BRGMODRST_F2H;
    munmap((void *)rst, 4096);
}

static void sleep_ms(long ms)
{
    struct timespec ts = { ms / 1000, (ms % 1000) * 1000000L };

    nanosleep(&ts, NULL);
}

/* Map the CSRs and check the bitstream. */
static volatile uint32_t *open_csr(uint32_t sensor_width)
{
    volatile uint32_t *csr = map_phys(LW_BRIDGE_BASE, 4096);
    uint32_t id, w;

    if (!csr)
        return NULL;
    id = csr[CSR_ID];
    if (id != CAM_ID) {
        fprintf(stderr, "camd: no cam_frame_writer at 0x%08x (ID 0x%08x%s)\n",
                LW_BRIDGE_BASE, id, id == CAM_ID_M1 ? ", an M1 bitstream: rebuild" : "");
        goto fail;
    }
    w = csr[CSR_SENSOR_WIDTH];
    if (w != sensor_width) {
        fprintf(stderr, "camd: FPGA SENSOR_WIDTH %u != %u\n", w, sensor_width);
        goto fail;
    }
    return csr;
fail:
    munmap((void *)csr, 4096);
    return NULL;
}

/* ── Sensor ──────────────────────────────────────────────────────────────── */

static void i2c_drive(void *ctx, int scl_low, int sda_low)
{
    volatile uint32_t *csr = ctx;

    csr[CSR_I2C] = (uint32_t)((scl_low ? 1 : 0) | (sda_low ? 2 : 0));
}

static int i2c_sense(void *ctx)
{
    volatile uint32_t *csr = ctx;
    uint32_t v = csr[CSR_I2C];

    return (int)((v >> 8) & 1) | (int)((v >> 9) & 1) << 1;
}

/* Wait for `n` more sensor frames; returns 0, or -1 after `timeout_ms`. */
static int wait_cap_frames(volatile uint32_t *csr, uint32_t n, long timeout_ms)
{
    uint32_t start = csr[CSR_CAP_FRAMES];

    for (long t = 0; t < timeout_ms; t += 5) {
        if (csr[CSR_CAP_FRAMES] - start >= n)
            return 0;
        sleep_ms(5);
    }
    return -1;
}

/* Clock the sensor, pulse RESETn, and look for it on I2C. */
static int sensor_power_up(volatile uint32_t *csr, struct i2c_bus *bus, uint16_t *version)
{
    *bus = (struct i2c_bus){
        .ctx = (void *)csr, .drive = i2c_drive, .sense = i2c_sense,
        .half_period_ns = 5000,
    };
    csr[CSR_I2C] = 0;
    csr[CSR_SENSOR_CTRL] = SENSOR_XCLK;               /* in reset, clocked */
    sleep_ms(10);
    csr[CSR_SENSOR_CTRL] = SENSOR_XCLK | SENSOR_RESET_N;
    sleep_ms(10);
    if (mt9p001_probe(bus, version) < 0 || (*version >> 8) != 0x18) {
        csr[CSR_SENSOR_CTRL] = 0;
        return -1;
    }
    return mt9p001_setup(bus);
}

static struct mt9p001_mode sensor_mode(const struct fpga *f, const struct image_cfg *img)
{
    return (struct mt9p001_mode){
        .width = img->width, .height = img->height,
        .offset_x = img->offset_x, .offset_y = img->offset_y,
        .frame_rate = img->frame_rate, .exposure_us = f->opts.exposure_us,
        .gain = f->opts.gain, .pixclk_hz = PIXCLK_HZ, .hb_min = f->hb_min,
    };
}

/* HBmin from the line period of a full-width frame with Horizontal_Blank = 0
 * (LROW = W/2 + HBmin). The datasheet's value depends on the revision. */
static void sensor_calibrate(struct fpga *f)
{
    struct image_cfg full = {
        .width = f->sensor_width, .height = f->sensor_height, .frame_rate = 1000.0f,
    };
    struct mt9p001_mode m = sensor_mode(f, &full);
    struct mt9p001_regs r;
    uint32_t period, size;

    m.exposure_us = 1000;
    mt9p001_compute(&m, &r);
    if (mt9p001_apply(&f->i2c, &r) < 0)
        return;
    f->csr[CSR_SENSOR_CTRL] = SENSOR_XCLK | SENSOR_RESET_N | SENSOR_CAPTURE;
    if (wait_cap_frames(f->csr, 3, 3000) < 0) {
        fprintf(stderr, "camd: sensor answers on I2C but sends no frames "
                "(check PIXCLK, FVAL, LVAL)\n");
        return;
    }
    period = f->csr[CSR_CAP_LINE_PERIOD];
    size = f->csr[CSR_CAP_SIZE];
    fprintf(stderr, "camd: sensor frame %ux%u, line %u PIXCLKs, frame %.1f ms\n",
            size >> 16, size & 0xFFF, period, f->csr[CSR_FRAME_PERIOD] / 50e3);
    if (period / 2 > f->sensor_width / 2 && period / 2 - f->sensor_width / 2 < 4096)
        f->hb_min = period / 2 - f->sensor_width / 2;
}

/* ── Writer ──────────────────────────────────────────────────────────────── */

static void stop_writer(struct fpga *f)
{
    f->csr[CSR_CTRL] = 0;
    for (int i = 0; i < 2000 && (f->csr[CSR_STATUS] & 1); i++)
        sleep_ms(1);
    f->running = 0;
}

static int same_cfg(const struct image_cfg *a, const struct image_cfg *b)
{
    return a->width == b->width && a->height == b->height &&
           a->offset_x == b->offset_x && a->offset_y == b->offset_y &&
           a->pixel_format == b->pixel_format && a->test_pattern == b->test_pattern &&
           a->frame_rate == b->frame_rate;
}

static int start_writer(struct fpga *f, const struct image_cfg *img)
{
    stop_writer(f);
    f->timeout_ns = FRAME_TIMEOUT_NS;
    if (img->test_pattern == TEST_PATTERN_OFF) {
        struct mt9p001_mode m = sensor_mode(f, img);
        struct mt9p001_regs r;

        if (!f->sensor) {
            if (!f->warned_no_sensor)
                fprintf(stderr, "camd: TestPattern Off needs the D5M sensor\n");
            f->warned_no_sensor = 1;
            return -1;
        }
        mt9p001_compute(&m, &r);
        if (mt9p001_apply(&f->i2c, &r) < 0) {
            fprintf(stderr, "camd: sensor did not acknowledge its configuration\n");
            return -1;
        }
        f->csr[CSR_FRAME_INTERVAL] = 0;               /* the sensor paces frames */
        f->timeout_ns += (uint64_t)(3e9 / r.frame_rate);
        fprintf(stderr, "camd: sensor %ux%u+%u+%u at %.3f fps, exposure %.0f us, "
                "gain 0x%02x\n", img->width, img->height, img->offset_x, img->offset_y,
                r.frame_rate, r.exposure_us, r.gain);
    } else {
        f->csr[CSR_FRAME_INTERVAL] = (uint32_t)(FABRIC_CLK_HZ / img->frame_rate);
    }
    f->csr[CSR_WIDTH] = img->width;
    f->csr[CSR_HEIGHT] = img->height;
    f->csr[CSR_OFFSET_X] = img->offset_x;
    f->csr[CSR_OFFSET_Y] = img->offset_y;
    f->csr[CSR_PATTERN] = img->test_pattern;
    f->csr[CSR_FORMAT] = img->pixel_format == PFNC_MONO8 ? 1 : 0;
    f->csr[CSR_BUF_BASE] = f->fb_base;
    f->csr[CSR_BUF_STRIDE] = f->stride;
    f->csr[CSR_BUF_COUNT] = f->nbufs;
    f->csr[CSR_CTRL] = 1;
    f->applied = *img;
    f->running = 1;
    f->last_count = 0;
    return 0;
}

static const uint8_t *fpga_next(struct frame_source *src, const struct image_cfg *img,
                                uint32_t n)
{
    struct fpga *f = src->priv;
    uint64_t deadline;
    uint32_t count, status;

    (void)n;
    if (!f->running || !same_cfg(&f->applied, img)) {
        if (start_writer(f, img) < 0) {
            sleep_ms(500);                            /* don't spin on the error */
            return NULL;
        }
    }

    deadline = now_ns() + f->timeout_ns;
    while ((count = f->csr[CSR_FRAME_COUNT]) == f->last_count) {
        if (now_ns() > deadline) {
            fprintf(stderr, "camd: no frame from FPGA within %.1f s (STATUS 0x%08x, "
                    "sensor frames %u, bad %u)\n", f->timeout_ns / 1e9,
                    f->csr[CSR_STATUS], f->csr[CSR_CAP_FRAMES], f->csr[CSR_BAD_FRAMES]);
            f->running = 0;                           /* reconfigure on retry */
            return NULL;
        }
        sleep_ms(1);
    }
    status = f->csr[CSR_STATUS];
    if (status & 2)
        fprintf(stderr, "camd: FPGA reported a DDR write error\n");
    if (count - f->last_count > 1 && f->last_count != 0)
        f->overruns += count - f->last_count - 1;   /* frames skipped by the streamer */
    f->last_count = count;
    return f->fb + (size_t)((status >> 8) & 0xF) * f->stride;
}

static void fpga_idle(struct frame_source *src)
{
    struct fpga *f = src->priv;

    if (f->running)
        stop_writer(f);
}

static float fpga_max_frame_rate(struct frame_source *src, const struct image_cfg *img)
{
    struct fpga *f = src->priv;
    double max;

    if (img->test_pattern != TEST_PATTERN_OFF || !f->sensor)
        return FRAME_RATE_MAX;
    max = mt9p001_max_frame_rate(img->width, img->height, PIXCLK_HZ, f->hb_min);
    /* Round down to what a float register holds, so writing Max back works. */
    return max < FRAME_RATE_MAX ? (float)(max * 0.9999) : FRAME_RATE_MAX;
}

static void fpga_counters(struct frame_source *src, uint32_t *overflows, uint32_t *bad)
{
    struct fpga *f = src->priv;

    *overflows = f->csr[CSR_CAP_OVERFLOWS] - f->overflows0;
    *bad = f->csr[CSR_BAD_FRAMES] - f->bad0;
}

static void fpga_close(struct frame_source *src)
{
    struct fpga *f = src->priv;

    stop_writer(f);
    f->csr[CSR_SENSOR_CTRL] = 0;                      /* sensor in reset, unclocked */
    munmap(f->fb, f->fb_size);
    munmap((void *)f->csr, 4096);
    free(f);
    free(src);
}

struct frame_source *source_fpga_open(uint32_t sensor_width, uint32_t sensor_height,
                                      uint32_t fb_base, uint32_t fb_size,
                                      const struct sensor_opts *opts)
{
    struct frame_source *src;
    struct fpga *f;
    uint16_t version;

    if (!region_is_free(fb_base, fb_size) || !fpga_ready())
        return NULL;

    src = calloc(1, sizeof(*src));
    f = calloc(1, sizeof(*f));
    if (!src || !f)
        goto fail;

    f->csr = open_csr(sensor_width);
    if (!f->csr)
        goto fail;

    f->fb_base = fb_base;
    f->fb_size = fb_size;
    f->stride = ((sensor_width * sensor_height) + 4095) & ~4095u;
    f->nbufs = fb_size / f->stride > 16 ? 16 : fb_size / f->stride;
    if (f->nbufs < 2) {
        fprintf(stderr, "camd: frame-buffer region too small\n");
        goto fail;
    }
    f->fb = map_phys(fb_base, fb_size);
    if (!f->fb)
        goto fail;

    release_f2h_bridge();
    stop_writer(f);

    f->sensor_width = sensor_width;
    f->sensor_height = sensor_height;
    f->opts = *opts;
    f->hb_min = MT9P001_HBMIN_DEFAULT;
    if (sensor_power_up(f->csr, &f->i2c, &version) == 0) {
        f->sensor = 1;
        fprintf(stderr, "camd: MT9P001 chip version 0x%04x\n", version);
        sensor_calibrate(f);
        fprintf(stderr, "camd: HBmin %u, full-res max %.2f fps at %.0f MHz PIXCLK\n",
                f->hb_min, mt9p001_max_frame_rate(sensor_width, sensor_height, PIXCLK_HZ,
                                                  f->hb_min), PIXCLK_HZ / 1e6);
    } else {
        fprintf(stderr, "camd: no MT9P001 on I2C (is the D5M on JP1?); "
                "test patterns only\n");
    }
    f->overflows0 = f->csr[CSR_CAP_OVERFLOWS];
    f->bad0 = f->csr[CSR_BAD_FRAMES];

    *src = (struct frame_source){
        .name = "fpga", .paced = 0, .next = fpga_next, .idle = fpga_idle,
        .max_frame_rate = fpga_max_frame_rate, .counters = fpga_counters,
        .close = fpga_close, .has_sensor = f->sensor, .priv = f,
    };
    fprintf(stderr, "camd: FPGA source, %u buffers of %u bytes at 0x%08x\n",
            f->nbufs, f->stride, fb_base);
    return src;
fail:
    if (f && f->csr)
        munmap((void *)f->csr, 4096);
    free(f);
    free(src);
    return NULL;
}

/* ── camd --sensor-test ──────────────────────────────────────────────────── */

#define SENSOR_TEST_FRAMES 20

/*
 * The sensor's color-field test pattern makes every pixel the same 12-bit
 * value, so OR = AND = value over a frame if all 12 data lines work. All-0,
 * all-1 and alternating values find stuck lines; walking ones find lines
 * shorted together or swapped.
 */
int source_fpga_sensor_test(void)
{
    static const uint16_t fixed[] = { 0x000, 0xFFF, 0x555, 0xAAA };
    volatile uint32_t *csr;
    struct fpga f = { 0 };
    struct image_cfg roi = { .width = 640, .height = 480, .frame_rate = 1000.0f };
    struct mt9p001_mode m;
    struct mt9p001_regs r;
    uint16_t version, values[16];
    uint32_t stuck0 = 0, stuck1 = 0, total_glitches = 0;
    int n = 0, bad = 0;

    if (!fpga_ready() || !(csr = open_csr(2592)))
        return 2;
    if (sensor_power_up(csr, &f.i2c, &version) < 0) {
        fprintf(stderr, "sensor-test: no MT9P001 on I2C (SCLK/SDATA, RESETn, XCLKIN)\n");
        return 1;
    }
    printf("MT9P001 chip version 0x%04x\n", version);

    f.csr = csr;
    f.hb_min = MT9P001_HBMIN_DEFAULT;
    f.opts = (struct sensor_opts){ .exposure_us = 1000, .gain = 1.0 };
    m = sensor_mode(&f, &roi);
    mt9p001_compute(&m, &r);
    if (mt9p001_apply(&f.i2c, &r) < 0)
        return 1;
    csr[CSR_SENSOR_CTRL] = SENSOR_XCLK | SENSOR_RESET_N | SENSOR_CAPTURE;
    if (wait_cap_frames(csr, 2, 3000) < 0) {
        fprintf(stderr, "sensor-test: no frames (PIXCLK, FVAL, LVAL)\n");
        return 1;
    }
    printf("frames arrive: %ux%u, %u PIXCLKs per line\n", csr[CSR_CAP_SIZE] >> 16,
           csr[CSR_CAP_SIZE] & 0xFFF, csr[CSR_CAP_LINE_PERIOD]);

    for (size_t i = 0; i < sizeof(fixed) / sizeof(fixed[0]); i++)
        values[n++] = fixed[i];
    for (int bit = 0; bit < 12; bit++)
        values[n++] = (uint16_t)(1u << bit);

    /* Per value: every frame must be 640x480 with OR = AND = value. A frame
     * with a wrong size or irregular lines means LINE_VALID/FRAME_VALID or
     * PIXCLK glitched (signal integrity, plan risk R3). */
    printf("value  OR     AND    glitched frames\n");
    for (int i = 0; i < n; i++) {
        uint32_t or_ = 0, and_ = 0xFFF, glitches = 0, seen;

        mt9p001_color_field(&f.i2c, 1, values[i]);
        wait_cap_frames(csr, 3, 3000);                /* let the change settle */
        seen = csr[CSR_CAP_FRAMES];
        for (int k = 0; k < SENSOR_TEST_FRAMES; k++) {
            uint32_t bits, size;

            if (wait_cap_frames(csr, 1, 3000) < 0)
                break;
            if (csr[CSR_CAP_FRAMES] - seen > 1)
                k += (int)(csr[CSR_CAP_FRAMES] - seen - 1);   /* missed polls */
            seen = csr[CSR_CAP_FRAMES];
            bits = csr[CSR_CAP_BITS];
            size = csr[CSR_CAP_SIZE];
            or_ |= bits & 0xFFF;
            and_ &= (bits >> 16) & 0xFFF;
            if (size != (640u << 16 | 480u) || csr[CSR_CAP_FLAGS])
                glitches++;
        }
        printf("0x%03x  0x%03x  0x%03x  %u/%d%s\n", values[i], or_, and_, glitches,
               SENSOR_TEST_FRAMES,
               or_ == values[i] && and_ == values[i] ? "" : "  <-- data mismatch");
        if (or_ != values[i] || and_ != values[i])
            bad++;
        total_glitches += glitches;
        stuck1 |= and_ & ~values[i];
        stuck0 |= ~or_ & values[i] & 0xFFF;
    }
    mt9p001_color_field(&f.i2c, 0, 0);
    csr[CSR_SENSOR_CTRL] = 0;

    if (!bad && !total_glitches) {
        printf("PASS: all 12 data lines, no glitched frames\n");
        return 0;
    }
    if (bad)
        printf("FAIL: lines read 1 when 0 (stuck high or shorted): 0x%03x, "
               "read 0 when 1 (stuck low or open): 0x%03x\n", stuck1, stuck0);
    if (total_glitches)
        printf("FAIL: %u glitched frames (sync or clock integrity)\n", total_glitches);
    return 1;
}

/* ── camd --sensor-reg ───────────────────────────────────────────────────── */

int source_fpga_sensor_regs(char **specs, int n)
{
    volatile uint32_t *csr;
    struct i2c_bus bus;
    uint16_t version;
    int rc = 0;

    if (!fpga_ready() || !(csr = open_csr(2592)))
        return 2;
    bus = (struct i2c_bus){
        .ctx = (void *)csr, .drive = i2c_drive, .sense = i2c_sense,
        .half_period_ns = 5000,
    };
    /* Power the sensor up only if it is held in reset (camd not running). */
    if (!(csr[CSR_SENSOR_CTRL] & SENSOR_RESET_N)) {
        if (sensor_power_up(csr, &bus, &version) < 0) {
            fprintf(stderr, "sensor-reg: no MT9P001 on I2C\n");
            return 1;
        }
        csr[CSR_SENSOR_CTRL] = SENSOR_XCLK | SENSOR_RESET_N | SENSOR_CAPTURE;
    }
    for (int i = 0; i < n; i++) {
        char *eq;
        unsigned long reg = strtoul(specs[i], &eq, 0);
        uint16_t v;

        if (*eq == '=') {
            v = (uint16_t)strtoul(eq + 1, NULL, 0);
            if (i2c_write16(&bus, MT9P001_I2C_ADDR, (uint8_t)reg, v) < 0) {
                printf("R0x%02lx write: no ACK\n", reg);
                rc = 1;
            } else {
                printf("R0x%02lx <- 0x%04x\n", reg, v);
            }
        } else if (i2c_read16(&bus, MT9P001_I2C_ADDR, (uint8_t)reg, &v) < 0) {
            printf("R0x%02lx read: no ACK\n", reg);
            rc = 1;
        } else {
            printf("R0x%02lx = 0x%04x\n", reg, v);
        }
    }
    if (wait_cap_frames(csr, 3, 3000) == 0) {
        uint32_t size = csr[CSR_CAP_SIZE], bits = csr[CSR_CAP_BITS];

        printf("capture: %ux%u, %u PIXCLKs per line, frame %.1f ms, "
               "OR 0x%03x AND 0x%03x, flags %u\n", size >> 16, size & 0xFFF,
               csr[CSR_CAP_LINE_PERIOD], csr[CSR_FRAME_PERIOD] / 50e3,
               bits & 0xFFF, (bits >> 16) & 0xFFF, csr[CSR_CAP_FLAGS]);
    } else {
        printf("capture: no frames\n");
    }
    return rc;
}

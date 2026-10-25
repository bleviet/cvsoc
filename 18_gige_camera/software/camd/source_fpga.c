/*
 * source_fpga.c — frames written into reserved DDR by cam_tpg_writer (see
 * hdl/cam_tpg_writer.vhd for the CSR map).
 *
 * The FPGA fills a ring of buffers in memory that Linux does not manage
 * (boot with mem=960M, buffers at 0x3C000000). camd waits for FRAME_COUNT to
 * advance and streams the buffer STATUS names as last completed. The mapping
 * is uncached (/dev/mem above the mem= limit), which ADR-001 measured as
 * fast enough for M1.
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

#include "source.h"
#include "util.h"

#define LW_BRIDGE_BASE   0xFF200000u
#define FPGAMGR_BASE     0xFF706000u
#define RSTMGR_PAGE      0xFFD05000u
#define BRGMODRST_OFF    0x01Cu
#define BRGMODRST_F2H    (1u << 2)
#define FPGAMGR_USER_MODE 0x4u

#define FABRIC_CLK_HZ    50000000u
#define CAM_ID           0x43564331u
#define FRAME_TIMEOUT_NS 2000000000ull

/* cam_tpg_writer CSRs (word offsets) */
enum {
    CSR_ID, CSR_CTRL, CSR_STATUS, CSR_FRAME_COUNT, CSR_WIDTH, CSR_HEIGHT,
    CSR_OFFSET_X, CSR_OFFSET_Y, CSR_PATTERN, CSR_FORMAT, CSR_FRAME_INTERVAL,
    CSR_BUF_BASE, CSR_BUF_STRIDE, CSR_BUF_COUNT, CSR_SENSOR_WIDTH, CSR_FRAME_CYCLES,
};

struct fpga {
    volatile uint32_t *csr;
    uint8_t  *fb;
    uint32_t  fb_base, fb_size, stride, nbufs;
    struct image_cfg applied;
    int       running;
    uint32_t  last_count;
    uint64_t  overruns;
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

static void start_writer(struct fpga *f, const struct image_cfg *img)
{
    stop_writer(f);
    f->csr[CSR_WIDTH] = img->width;
    f->csr[CSR_HEIGHT] = img->height;
    f->csr[CSR_OFFSET_X] = img->offset_x;
    f->csr[CSR_OFFSET_Y] = img->offset_y;
    f->csr[CSR_PATTERN] = img->test_pattern;
    f->csr[CSR_FORMAT] = img->pixel_format == PFNC_MONO8 ? 1 : 0;
    f->csr[CSR_FRAME_INTERVAL] = (uint32_t)(FABRIC_CLK_HZ / img->frame_rate);
    f->csr[CSR_BUF_BASE] = f->fb_base;
    f->csr[CSR_BUF_STRIDE] = f->stride;
    f->csr[CSR_BUF_COUNT] = f->nbufs;
    f->csr[CSR_CTRL] = 1;
    f->applied = *img;
    f->running = 1;
    f->last_count = 0;
}

static const uint8_t *fpga_next(struct frame_source *src, const struct image_cfg *img,
                                uint32_t n)
{
    struct fpga *f = src->priv;
    uint64_t deadline = now_ns() + FRAME_TIMEOUT_NS;
    uint32_t count, status;

    (void)n;
    if (!f->running || !same_cfg(&f->applied, img))
        start_writer(f, img);

    while ((count = f->csr[CSR_FRAME_COUNT]) == f->last_count) {
        if (now_ns() > deadline) {
            fprintf(stderr, "camd: no frame from FPGA within 2 s (STATUS 0x%08x)\n",
                    f->csr[CSR_STATUS]);
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

static void fpga_close(struct frame_source *src)
{
    struct fpga *f = src->priv;

    stop_writer(f);
    munmap(f->fb, f->fb_size);
    munmap((void *)f->csr, 4096);
    free(f);
    free(src);
}

struct frame_source *source_fpga_open(uint32_t sensor_width, uint32_t sensor_height,
                                      uint32_t fb_base, uint32_t fb_size)
{
    struct frame_source *src;
    struct fpga *f;
    uint32_t hw_width;

    if (!region_is_free(fb_base, fb_size) || !fpga_ready())
        return NULL;

    src = calloc(1, sizeof(*src));
    f = calloc(1, sizeof(*f));
    if (!src || !f)
        goto fail;

    f->csr = map_phys(LW_BRIDGE_BASE, 4096);
    if (!f->csr)
        goto fail;
    if (f->csr[CSR_ID] != CAM_ID) {
        fprintf(stderr, "camd: no cam_tpg_writer at 0x%08x (ID 0x%08x)\n",
                LW_BRIDGE_BASE, f->csr[CSR_ID]);
        goto fail;
    }
    hw_width = f->csr[CSR_SENSOR_WIDTH];
    if (hw_width != sensor_width) {
        fprintf(stderr, "camd: FPGA SENSOR_WIDTH %u != %u\n", hw_width, sensor_width);
        goto fail;
    }

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

    *src = (struct frame_source){
        .name = "fpga", .paced = 0, .next = fpga_next, .idle = fpga_idle,
        .close = fpga_close, .priv = f,
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

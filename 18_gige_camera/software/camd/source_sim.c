/*
 * source_sim.c — software test-pattern frame source (see source.h).
 */
#include <stdlib.h>

#include "source.h"

static const uint8_t color_bars[8][3] = {
    { 255, 255, 255 }, { 255, 255, 0 }, { 0, 255, 255 }, { 0, 255, 0 },
    { 255, 0, 255 },   { 255, 0, 0 },   { 0, 0, 255 },   { 0, 0, 0 },
};

uint8_t tpg_pixel(uint32_t pattern, uint32_t pixel_format, uint32_t x, uint32_t y,
                  uint32_t n, uint32_t sensor_width)
{
    const uint8_t *rgb;

    switch (pattern) {
    case TEST_PATTERN_GREY_HRAMP_MOVING:
        return (uint8_t)(x + n);
    case TEST_PATTERN_COLOR_BAR:
        rgb = color_bars[(uint64_t)x * 8 / sensor_width];
        if (pixel_format == PFNC_MONO8)
            return (uint8_t)((77 * rgb[0] + 150 * rgb[1] + 29 * rgb[2]) >> 8);
        if ((y & 1) == 0)
            return (x & 1) ? rgb[0] : rgb[1];   /* G R */
        return (x & 1) ? rgb[1] : rgb[2];       /* B G */
    default:
        return 0;
    }
}

struct sim {
    uint8_t *buf;
    uint32_t sensor_width;
};

static const uint8_t *sim_next(struct frame_source *src, const struct image_cfg *img,
                               uint32_t n)
{
    struct sim *s = src->priv;
    uint8_t *p = s->buf;

    for (uint32_t y = 0; y < img->height; y++)
        for (uint32_t x = 0; x < img->width; x++)
            *p++ = tpg_pixel(img->test_pattern, img->pixel_format,
                             x + img->offset_x, y + img->offset_y, n, s->sensor_width);
    return s->buf;
}

static void sim_close(struct frame_source *src)
{
    struct sim *s = src->priv;

    free(s->buf);
    free(s);
    free(src);
}

struct frame_source *source_sim_open(uint32_t sensor_width, uint32_t sensor_height)
{
    struct frame_source *src = calloc(1, sizeof(*src));
    struct sim *s = calloc(1, sizeof(*s));

    if (!src || !s)
        goto fail;
    s->buf = malloc((size_t)sensor_width * sensor_height);
    if (!s->buf)
        goto fail;
    s->sensor_width = sensor_width;
    *src = (struct frame_source){
        .name = "sim", .paced = 1, .next = sim_next, .close = sim_close, .priv = s,
    };
    return src;
fail:
    free(s);
    free(src);
    return NULL;
}

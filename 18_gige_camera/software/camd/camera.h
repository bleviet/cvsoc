/*
 * camera.h — camd's device model: bootstrap registers, camera features,
 * the GenICam XML, control-channel privilege, and heartbeat.
 *
 * All state is behind `lock`. The GVCP thread changes it; the stream thread
 * waits on `changed` and takes snapshots.
 */
#ifndef CAMERA_H
#define CAMERA_H

#include <netinet/in.h>
#include <pthread.h>
#include <stddef.h>
#include <stdint.h>

#include "gev.h"

/* ── Device-specific registers (described by genicam/cvsoc_camera.xml) ────── */
#define REG_WIDTH_MAX           0x00010000  /* RO */
#define REG_HEIGHT_MAX          0x00010004  /* RO */
#define REG_WIDTH               0x00010008
#define REG_HEIGHT              0x0001000C
#define REG_OFFSET_X            0x00010010
#define REG_OFFSET_Y            0x00010014
#define REG_PIXEL_FORMAT        0x00010018
#define REG_PAYLOAD_SIZE        0x0001001C  /* RO */
#define REG_ACQ_MODE            0x00010020
#define REG_ACQ_START           0x00010024  /* write 1 */
#define REG_ACQ_STOP            0x00010028  /* write 1 */
#define REG_FRAME_RATE          0x0001002C  /* IEEE 754 float, frames/s */
#define REG_TEST_PATTERN        0x00010030
#define REG_TL_PARAMS_LOCKED    0x00010034
#define REG_FRAMES_SENT         0x00010038  /* RO, diagnostics */
#define REG_LAST                REG_FRAMES_SENT

#define XML_ADDR                0x00100000

#define WIDTH_MIN       64
#define WIDTH_INC       8
#define HEIGHT_MIN      2
#define HEIGHT_INC      2
#define FRAME_RATE_MIN  1.0f
#define FRAME_RATE_MAX  60.0f
#define PACKET_SIZE_MIN 576

enum test_pattern {
    TEST_PATTERN_OFF = 0,               /* sensor image; black until M2 */
    TEST_PATTERN_GREY_HRAMP_MOVING = 1,
    TEST_PATTERN_COLOR_BAR = 2,
};

struct image_cfg {
    uint32_t width, height, offset_x, offset_y;
    uint32_t pixel_format;
    uint32_t test_pattern;
    float    frame_rate;
};

struct stream_cfg {
    struct sockaddr_in dest;           /* sin_port == 0: not configured */
    uint32_t packet_size;              /* incl. IP/UDP/GVSP headers */
    int      do_not_fragment;
    uint32_t packet_delay;
};

struct cam_identity {
    const char *manufacturer, *model, *version, *info, *serial;
    uint32_t sensor_width, sensor_height;
    uint32_t max_packet_size;
    const uint8_t *xml;
    size_t   xml_len;
    const char *xml_name;
};

struct camera {
    pthread_mutex_t lock;
    pthread_cond_t  changed;

    uint8_t  bs[BS_SIZE];              /* bootstrap block, big-endian image */
    struct image_cfg img;
    int      acquiring;
    uint32_t tl_locked;
    uint32_t sensor_width, sensor_height;
    uint32_t max_packet_size;
    uint32_t frames_sent;
    int      fire_test_packet;

    int      owned;                    /* control channel privilege held */
    struct sockaddr_in owner;
    uint64_t owner_seen_ns;

    const uint8_t *xml;
    size_t   xml_len;
    uint64_t ts_epoch_ns;              /* timestamp counter reset point */
};

void     cam_init(struct camera *c, const struct cam_identity *id);
void     cam_set_network(struct camera *c, const uint8_t mac[6],
                         struct in_addr ip, struct in_addr mask);
uint64_t cam_timestamp(const struct camera *c);   /* ns since reset */

/* GVCP-facing accessors; return a GEV_STATUS_* code. Caller holds no lock.
 * A NULL `peer` bypasses access control (used for the discovery ack). */
uint16_t cam_read(struct camera *c, uint32_t addr, uint8_t *out, size_t len,
                  const struct sockaddr_in *peer);
uint16_t cam_write_reg(struct camera *c, uint32_t addr, uint32_t value,
                       const struct sockaddr_in *peer);
uint16_t cam_write_mem(struct camera *c, uint32_t addr, const uint8_t *data,
                       size_t len, const struct sockaddr_in *peer);

/* Refresh the heartbeat if `peer` is the controlling application. */
void     cam_seen(struct camera *c, const struct sockaddr_in *peer, uint64_t now_ns);
/* Release control and stop streaming if the heartbeat expired. */
int      cam_heartbeat_check(struct camera *c, uint64_t now_ns);

/* Snapshot for the stream thread. The caller holds `lock` (it waits on
 * `changed` between frames). */
void     cam_snapshot(struct camera *c, struct image_cfg *img, struct stream_cfg *st,
                      int *acquiring);
uint32_t cam_payload_size(const struct image_cfg *img);

#endif /* CAMERA_H */

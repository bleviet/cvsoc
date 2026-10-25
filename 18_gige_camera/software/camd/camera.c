/*
 * camera.c — camd's device model. See camera.h.
 */
#define _GNU_SOURCE
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "camera.h"
#include "util.h"

static uint32_t bs_get(const struct camera *c, uint32_t addr)
{
    return get_be32(&c->bs[addr]);
}

static void bs_set(struct camera *c, uint32_t addr, uint32_t v)
{
    put_be32(&c->bs[addr], v);
}

static void bs_set_string(struct camera *c, uint32_t addr, size_t max, const char *s)
{
    size_t n = s ? strnlen(s, max - 1) : 0;

    memset(&c->bs[addr], 0, max);
    memcpy(&c->bs[addr], s, n);
}

static uint32_t float_bits(float f)
{
    uint32_t u;

    memcpy(&u, &f, sizeof(u));
    return u;
}

static float bits_float(uint32_t u)
{
    float f;

    memcpy(&f, &u, sizeof(f));
    return f;
}

static int same_peer(const struct sockaddr_in *a, const struct sockaddr_in *b)
{
    return a->sin_addr.s_addr == b->sin_addr.s_addr && a->sin_port == b->sin_port;
}

uint32_t cam_payload_size(const struct image_cfg *img)
{
    return img->width * img->height;   /* all M1 formats are 8 bits/pixel */
}

uint64_t cam_timestamp(const struct camera *c)
{
    return now_ns() - c->ts_epoch_ns;
}

void cam_init(struct camera *c, const struct cam_identity *id)
{
    char url[512];

    memset(c, 0, sizeof(*c));
    pthread_mutex_init(&c->lock, NULL);
    pthread_cond_init(&c->changed, NULL);

    c->sensor_width = id->sensor_width;
    c->sensor_height = id->sensor_height;
    c->max_packet_size = id->max_packet_size;
    c->xml = id->xml;
    c->xml_len = id->xml_len;
    c->ts_epoch_ns = now_ns();

    c->img = (struct image_cfg){
        .width = id->sensor_width, .height = id->sensor_height,
        .pixel_format = PFNC_BAYER_GR8,
        .test_pattern = TEST_PATTERN_GREY_HRAMP_MOVING,
        .frame_rate = 10.0f,
    };

    bs_set(c, BS_VERSION, 0x00010002);            /* GigE Vision 1.2 */
    bs_set(c, BS_DEVICE_MODE, DEVICE_MODE_BIG_ENDIAN | DEVICE_MODE_UTF8);
    bs_set(c, BS_NET_CAPABILITY, NET_CAP_LLA | NET_CAP_DHCP | NET_CAP_PERSISTENT_IP);
    bs_set(c, BS_NET_CONFIG, NET_CAP_LLA | NET_CAP_DHCP);
    bs_set_string(c, BS_MANUFACTURER_NAME, 32, id->manufacturer);
    bs_set_string(c, BS_MODEL_NAME, 32, id->model);
    bs_set_string(c, BS_DEVICE_VERSION, 32, id->version);
    bs_set_string(c, BS_MANUFACTURER_INFO, 48, id->info);
    bs_set_string(c, BS_SERIAL_NUMBER, 16, id->serial);

    snprintf(url, sizeof(url), "Local:%s;%x;%zx", id->xml_name, XML_ADDR, id->xml_len);
    bs_set_string(c, BS_FIRST_URL, 512, url);

    bs_set(c, BS_NUM_NET_INTERFACES, 1);
    bs_set(c, BS_NUM_MESSAGE_CHANNELS, 0);
    bs_set(c, BS_NUM_STREAM_CHANNELS, 1);
    bs_set(c, BS_GVCP_CAPABILITY, GVCP_CAP_USER_NAME | GVCP_CAP_SERIAL_NUMBER |
                                  GVCP_CAP_HEARTBEAT_DIS | GVCP_CAP_WRITEMEM |
                                  GVCP_CAP_CONCATENATION);
    bs_set(c, BS_HEARTBEAT_TIMEOUT, 3000);
    bs_set(c, BS_TS_FREQ_HIGH, 0);
    bs_set(c, BS_TS_FREQ_LOW, 1000000000u);       /* 1 tick = 1 ns */
    bs_set(c, BS_SCPS0, id->max_packet_size < 1500 ? id->max_packet_size : 1500);
}

void cam_set_network(struct camera *c, const uint8_t mac[6], struct in_addr ip,
                     struct in_addr mask)
{
    pthread_mutex_lock(&c->lock);
    bs_set(c, BS_MAC_HIGH, (uint32_t)mac[0] << 8 | mac[1]);
    bs_set(c, BS_MAC_LOW, get_be32(&mac[2]));
    bs_set(c, BS_CURRENT_IP, ntohl(ip.s_addr));
    bs_set(c, BS_CURRENT_MASK, ntohl(mask.s_addr));
    pthread_mutex_unlock(&c->lock);
}

/* Stop streaming and give up control: on release and on heartbeat expiry. */
static void release_control(struct camera *c)
{
    c->owned = 0;
    c->acquiring = 0;
    bs_set(c, BS_CCP, 0);
    bs_set(c, BS_SCP0, 0);
    pthread_cond_broadcast(&c->changed);
}

/* ── Reads ────────────────────────────────────────────────────────────────── */

static uint16_t read_word(const struct camera *c, uint32_t addr, uint32_t *v)
{
    if (addr < BS_SIZE) {
        *v = bs_get(c, addr);
        return GEV_STATUS_SUCCESS;
    }
    if (addr >= XML_ADDR && addr < XML_ADDR + ((c->xml_len + 3) & ~(size_t)3)) {
        uint8_t w[4] = { 0 };
        size_t off = addr - XML_ADDR;

        for (int i = 0; i < 4 && off + i < c->xml_len; i++)
            w[i] = c->xml[off + i];
        *v = get_be32(w);
        return GEV_STATUS_SUCCESS;
    }

    switch (addr) {
    case REG_WIDTH_MAX:        *v = c->sensor_width; break;
    case REG_HEIGHT_MAX:       *v = c->sensor_height; break;
    case REG_WIDTH:            *v = c->img.width; break;
    case REG_HEIGHT:           *v = c->img.height; break;
    case REG_OFFSET_X:         *v = c->img.offset_x; break;
    case REG_OFFSET_Y:         *v = c->img.offset_y; break;
    case REG_PIXEL_FORMAT:     *v = c->img.pixel_format; break;
    case REG_PAYLOAD_SIZE:     *v = cam_payload_size(&c->img); break;
    case REG_ACQ_MODE:         *v = 0; break;
    case REG_ACQ_START:
    case REG_ACQ_STOP:         *v = 0; break;
    case REG_FRAME_RATE:       *v = float_bits(c->img.frame_rate); break;
    case REG_TEST_PATTERN:     *v = c->img.test_pattern; break;
    case REG_TL_PARAMS_LOCKED: *v = c->tl_locked; break;
    case REG_FRAMES_SENT:      *v = c->frames_sent; break;
    default:                   return GEV_STATUS_INVALID_ADDRESS;
    }
    return GEV_STATUS_SUCCESS;
}

uint16_t cam_read(struct camera *c, uint32_t addr, uint8_t *out, size_t len,
                  const struct sockaddr_in *peer)
{
    uint16_t st = GEV_STATUS_SUCCESS;

    pthread_mutex_lock(&c->lock);
    if (peer && c->owned && (bs_get(c, BS_CCP) & CCP_EXCLUSIVE) &&
        !same_peer(peer, &c->owner)) {
        st = GEV_STATUS_ACCESS_DENIED;
    } else {
        for (size_t i = 0; i < len; ) {
            uint32_t a = addr + (uint32_t)i, w = 0;
            uint8_t b[4];
            size_t skip = a & 3;

            st = read_word(c, a - (uint32_t)skip, &w);
            if (st != GEV_STATUS_SUCCESS)
                break;
            put_be32(b, w);
            for (size_t k = skip; k < 4 && i < len; k++)
                out[i++] = b[k];
        }
    }
    pthread_mutex_unlock(&c->lock);
    return st;
}

/* ── Writes ───────────────────────────────────────────────────────────────── */

static uint16_t write_image_reg(struct camera *c, uint32_t addr, uint32_t v)
{
    struct image_cfg *img = &c->img;

    if (c->acquiring)
        return GEV_STATUS_BUSY;

    switch (addr) {
    case REG_WIDTH:
        if (v < WIDTH_MIN || v % WIDTH_INC || v + img->offset_x > c->sensor_width)
            return GEV_STATUS_INVALID_PARAMETER;
        img->width = v;
        break;
    case REG_HEIGHT:
        if (v < HEIGHT_MIN || v % HEIGHT_INC || v + img->offset_y > c->sensor_height)
            return GEV_STATUS_INVALID_PARAMETER;
        img->height = v;
        break;
    case REG_OFFSET_X:
        if (v % WIDTH_INC || v + img->width > c->sensor_width)
            return GEV_STATUS_INVALID_PARAMETER;
        img->offset_x = v;
        break;
    case REG_OFFSET_Y:
        /* Even offsets keep the Bayer phase (GR) unchanged. */
        if (v % HEIGHT_INC || v + img->height > c->sensor_height)
            return GEV_STATUS_INVALID_PARAMETER;
        img->offset_y = v;
        break;
    case REG_PIXEL_FORMAT:
        if (v != PFNC_MONO8 && v != PFNC_BAYER_GR8)
            return GEV_STATUS_INVALID_PARAMETER;
        img->pixel_format = v;
        break;
    }
    return GEV_STATUS_SUCCESS;
}

static uint16_t write_reg_locked(struct camera *c, uint32_t addr, uint32_t v)
{
    uint32_t size;

    switch (addr) {
    /* Bootstrap: writable registers */
    case BS_NET_CONFIG:
    case BS_PERSISTENT_IP:
    case BS_PERSISTENT_MASK:
    case BS_PERSISTENT_GATEWAY:
    case BS_DISCOVERY_ACK_DELAY:
    case BS_GVCP_CONFIG:
    case BS_MCP:
    case BS_MCDA:
    case BS_MCTT:
    case BS_MCRC:
    case BS_SCPD0:
    case BS_SCCFG0:
        bs_set(c, addr, v);
        return GEV_STATUS_SUCCESS;
    case BS_USER_NAME:
    case BS_USER_NAME + 4:
    case BS_USER_NAME + 8:
    case BS_USER_NAME + 12:
        bs_set(c, addr, v);
        c->bs[BS_USER_NAME + 15] = 0;
        return GEV_STATUS_SUCCESS;
    case BS_HEARTBEAT_TIMEOUT:
        if (v < 500)
            return GEV_STATUS_INVALID_PARAMETER;
        bs_set(c, addr, v);
        return GEV_STATUS_SUCCESS;
    case BS_TS_CONTROL:
        if (v & TS_CONTROL_RESET)
            c->ts_epoch_ns = now_ns();
        if (v & TS_CONTROL_LATCH) {
            uint64_t ts = cam_timestamp(c);

            bs_set(c, BS_TS_VALUE_HIGH, (uint32_t)(ts >> 32));
            bs_set(c, BS_TS_VALUE_LOW, (uint32_t)ts);
        }
        return GEV_STATUS_SUCCESS;
    case BS_SCP0:
    case BS_SCDA0:
        bs_set(c, addr, v);
        pthread_cond_broadcast(&c->changed);
        return GEV_STATUS_SUCCESS;
    case BS_SCPS0:
        size = v & SCPS_SIZE_MASK;
        if (size < PACKET_SIZE_MIN)
            size = PACKET_SIZE_MIN;
        if (size > c->max_packet_size)
            size = c->max_packet_size;
        size &= ~3u;   /* keep payload data 32-bit aligned */
        bs_set(c, addr, (v & ~(SCPS_SIZE_MASK | SCPS_FIRE_TEST_PACKET)) | size);
        if (v & SCPS_FIRE_TEST_PACKET) {
            c->fire_test_packet = 1;
            pthread_cond_broadcast(&c->changed);
        }
        return GEV_STATUS_SUCCESS;

    /* Device registers */
    case REG_WIDTH:
    case REG_HEIGHT:
    case REG_OFFSET_X:
    case REG_OFFSET_Y:
    case REG_PIXEL_FORMAT:
        return write_image_reg(c, addr, v);
    case REG_ACQ_MODE:
        return v == 0 ? GEV_STATUS_SUCCESS : GEV_STATUS_INVALID_PARAMETER;
    case REG_ACQ_START:
        if (v == 1) {
            c->acquiring = 1;
            pthread_cond_broadcast(&c->changed);
        }
        return GEV_STATUS_SUCCESS;
    case REG_ACQ_STOP:
        if (v == 1) {
            c->acquiring = 0;
            pthread_cond_broadcast(&c->changed);
        }
        return GEV_STATUS_SUCCESS;
    case REG_FRAME_RATE: {
        float f = bits_float(v);

        if (!isfinite(f) || f < FRAME_RATE_MIN || f > FRAME_RATE_MAX)
            return GEV_STATUS_INVALID_PARAMETER;
        c->img.frame_rate = f;
        return GEV_STATUS_SUCCESS;
    }
    case REG_TEST_PATTERN:
        if (v > TEST_PATTERN_COLOR_BAR)
            return GEV_STATUS_INVALID_PARAMETER;
        c->img.test_pattern = v;
        return GEV_STATUS_SUCCESS;
    case REG_TL_PARAMS_LOCKED:
        if (v > 1)
            return GEV_STATUS_INVALID_PARAMETER;
        c->tl_locked = v;
        return GEV_STATUS_SUCCESS;
    case REG_WIDTH_MAX:
    case REG_HEIGHT_MAX:
    case REG_PAYLOAD_SIZE:
    case REG_FRAMES_SENT:
        return GEV_STATUS_WRITE_PROTECT;
    }

    if (addr < BS_SIZE || addr >= XML_ADDR)
        return GEV_STATUS_WRITE_PROTECT;
    return GEV_STATUS_INVALID_ADDRESS;
}

uint16_t cam_write_reg(struct camera *c, uint32_t addr, uint32_t v,
                       const struct sockaddr_in *peer)
{
    uint16_t st;

    if (addr & 3)
        return GEV_STATUS_BAD_ALIGNMENT;

    pthread_mutex_lock(&c->lock);
    if (addr == BS_CCP) {
        if (c->owned && !same_peer(peer, &c->owner)) {
            st = GEV_STATUS_ACCESS_DENIED;
        } else if (v & (CCP_CONTROL | CCP_EXCLUSIVE)) {
            c->owned = 1;
            c->owner = *peer;
            c->owner_seen_ns = now_ns();
            bs_set(c, BS_CCP, v & (CCP_CONTROL | CCP_EXCLUSIVE));
            st = GEV_STATUS_SUCCESS;
        } else {
            if (c->owned)
                release_control(c);
            st = GEV_STATUS_SUCCESS;
        }
    } else if (!c->owned || !same_peer(peer, &c->owner)) {
        st = GEV_STATUS_ACCESS_DENIED;   /* writes need control privilege */
    } else {
        st = write_reg_locked(c, addr, v);
    }
    pthread_mutex_unlock(&c->lock);
    return st;
}

uint16_t cam_write_mem(struct camera *c, uint32_t addr, const uint8_t *data,
                       size_t len, const struct sockaddr_in *peer)
{
    uint16_t st = GEV_STATUS_SUCCESS;

    if ((addr & 3) || (len & 3))
        return GEV_STATUS_BAD_ALIGNMENT;
    for (size_t i = 0; i < len && st == GEV_STATUS_SUCCESS; i += 4)
        st = cam_write_reg(c, addr + (uint32_t)i, get_be32(&data[i]), peer);
    return st;
}

/* ── Heartbeat ────────────────────────────────────────────────────────────── */

void cam_seen(struct camera *c, const struct sockaddr_in *peer, uint64_t now)
{
    pthread_mutex_lock(&c->lock);
    if (c->owned && same_peer(peer, &c->owner))
        c->owner_seen_ns = now;
    pthread_mutex_unlock(&c->lock);
}

int cam_heartbeat_check(struct camera *c, uint64_t now)
{
    int expired = 0;

    pthread_mutex_lock(&c->lock);
    if (c->owned && !(bs_get(c, BS_GVCP_CONFIG) & GVCP_CONFIG_HEARTBEAT_DISABLE) &&
        now - c->owner_seen_ns > (uint64_t)bs_get(c, BS_HEARTBEAT_TIMEOUT) * 1000000ull) {
        release_control(c);
        expired = 1;
    }
    pthread_mutex_unlock(&c->lock);
    return expired;
}

/* ── Stream thread snapshot ──────────────────────────────────────────────── */

void cam_snapshot(struct camera *c, struct image_cfg *img, struct stream_cfg *st,
                  int *acquiring)
{
    uint32_t scps;

    /* Caller holds c->lock (it waits on c->changed). */
    *img = c->img;
    *acquiring = c->acquiring;
    memset(&st->dest, 0, sizeof(st->dest));
    st->dest.sin_family = AF_INET;
    st->dest.sin_addr.s_addr = htonl(bs_get(c, BS_SCDA0));
    st->dest.sin_port = htons((uint16_t)(bs_get(c, BS_SCP0) & 0xFFFF));
    scps = bs_get(c, BS_SCPS0);
    st->packet_size = scps & SCPS_SIZE_MASK;
    st->do_not_fragment = !!(scps & SCPS_DO_NOT_FRAGMENT);
    st->packet_delay = bs_get(c, BS_SCPD0);
}

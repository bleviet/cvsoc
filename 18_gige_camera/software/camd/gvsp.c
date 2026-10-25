/*
 * gvsp.c — GVSP streamer thread (see gvsp.h).
 *
 * One frame is a block: a leader (packet 0), payload packets 1..N carrying
 * packet_size - 36 bytes each (the last one may be short), and a trailer
 * (packet N+1). Payload packets are sent scatter-gather from the frame
 * buffer with sendmmsg(), as measured in M0 (ADR-001).
 */
#define _GNU_SOURCE
#include <errno.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <unistd.h>

#include "gvsp.h"
#include "util.h"

#define BATCH 32

static void gvsp_header(uint8_t *h, uint16_t block_id, uint8_t format, uint32_t packet_id)
{
    put_be16(&h[0], 0);                             /* status */
    put_be16(&h[2], block_id);
    put_be32(&h[4], (uint32_t)format << 24 | (packet_id & 0xFFFFFF));
}

static int send_one(int fd, const struct sockaddr_in *dest, const uint8_t *buf, size_t len)
{
    for (;;) {
        if (sendto(fd, buf, len, 0, (const struct sockaddr *)dest, sizeof(*dest)) >= 0)
            return 0;
        if (errno == EINTR || errno == ENOBUFS || errno == EAGAIN)
            continue;
        return -1;
    }
}

static void set_dont_fragment(int fd, int on)
{
    int v = on ? IP_PMTUDISC_DO : IP_PMTUDISC_WANT;

    setsockopt(fd, IPPROTO_IP, IP_MTU_DISCOVER, &v, sizeof(v));
}

static int send_frame(struct streamer *s, const struct stream_cfg *st,
                      const struct image_cfg *img, const uint8_t *data, uint64_t ts)
{
    static struct mmsghdr msgs[BATCH];
    static struct iovec   iov[BATCH][2];
    static uint8_t        hdrs[BATCH][GVSP_HDR_LEN];
    uint8_t pkt[GVSP_HDR_LEN + 36];
    size_t size = cam_payload_size(img);
    size_t chunk = st->packet_size - GVSP_PACKET_OVERHEAD;
    uint32_t npackets = (uint32_t)((size + chunk - 1) / chunk);
    uint32_t id = 1;
    size_t off = 0;

    if (++s->block_id == 0)          /* block ID 0 is reserved */
        s->block_id = 1;

    /* Leader: image payload description. */
    gvsp_header(pkt, s->block_id, GVSP_FORMAT_LEADER, 0);
    put_be16(&pkt[8], 0);                           /* reserved */
    put_be16(&pkt[10], GVSP_PAYLOAD_TYPE_IMAGE);
    put_be32(&pkt[12], (uint32_t)(ts >> 32));
    put_be32(&pkt[16], (uint32_t)ts);
    put_be32(&pkt[20], img->pixel_format);
    put_be32(&pkt[24], img->width);
    put_be32(&pkt[28], img->height);
    put_be32(&pkt[32], img->offset_x);
    put_be32(&pkt[36], img->offset_y);
    put_be16(&pkt[40], 0);                          /* padding x */
    put_be16(&pkt[42], 0);                          /* padding y */
    if (send_one(s->fd, &st->dest, pkt, GVSP_HDR_LEN + 36) < 0)
        return -1;

    /* Payload packets. */
    while (id <= npackets) {
        int n = 0;

        while (n < BATCH && id + n <= npackets) {
            size_t len = size - off < chunk ? size - off : chunk;

            gvsp_header(hdrs[n], s->block_id, GVSP_FORMAT_PAYLOAD, id + n);
            iov[n][0] = (struct iovec){ hdrs[n], GVSP_HDR_LEN };
            iov[n][1] = (struct iovec){ (void *)(data + off), len };
            msgs[n].msg_hdr = (struct msghdr){
                .msg_name = (void *)&st->dest, .msg_namelen = sizeof(st->dest),
                .msg_iov = iov[n], .msg_iovlen = 2,
            };
            off += len;
            n++;
        }
        for (int done = 0; done < n; ) {
            int r = sendmmsg(s->fd, msgs + done, n - done, 0);

            if (r < 0) {
                if (errno == EINTR || errno == ENOBUFS || errno == EAGAIN)
                    continue;
                return -1;
            }
            done += r;
        }
        id += n;
    }

    /* Trailer. */
    gvsp_header(pkt, s->block_id, GVSP_FORMAT_TRAILER, npackets + 1);
    put_be16(&pkt[8], 0);
    put_be16(&pkt[10], GVSP_PAYLOAD_TYPE_IMAGE);
    put_be32(&pkt[12], img->height);
    return send_one(s->fd, &st->dest, pkt, GVSP_HDR_LEN + 8);
}

/* GevSCPSFireTestPacket: one datagram of exactly the configured packet size. */
static void send_test_packet(struct streamer *s, const struct stream_cfg *st)
{
    static uint8_t buf[9000];
    size_t len = st->packet_size - GVSP_IP_UDP_OVERHEAD;

    if (st->dest.sin_port == 0 || len > sizeof(buf))
        return;
    memset(buf, 0, len);
    set_dont_fragment(s->fd, 1);
    if (send_one(s->fd, &st->dest, buf, len) < 0 && s->verbose)
        perror("camd: test packet");
}

static void sleep_until(uint64_t t)
{
    struct timespec ts = { (time_t)(t / 1000000000ull), (long)(t % 1000000000ull) };

    while (clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &ts, NULL) == EINTR)
        ;
}

static void *stream_thread(void *arg)
{
    struct streamer *s = arg;
    struct camera *c = s->cam;
    struct image_cfg img;
    struct stream_cfg st;
    uint64_t next_due = 0;
    uint32_t frame_no = 0;
    int acquiring, was_acquiring = 0, df = -1;

    pthread_mutex_lock(&c->lock);
    while (!s->stop) {
        cam_snapshot(c, &img, &st, &acquiring);

        if (c->fire_test_packet) {
            c->fire_test_packet = 0;
            pthread_mutex_unlock(&c->lock);
            send_test_packet(s, &st);
            df = -1;
            pthread_mutex_lock(&c->lock);
            continue;
        }
        if (!acquiring || st.dest.sin_port == 0 || st.dest.sin_addr.s_addr == 0) {
            struct timespec until;

            if (was_acquiring && s->src->idle) {
                pthread_mutex_unlock(&c->lock);
                s->src->idle(s->src);
                pthread_mutex_lock(&c->lock);
            }
            was_acquiring = 0;

            clock_gettime(CLOCK_REALTIME, &until);
            until.tv_nsec += 100000000;
            if (until.tv_nsec >= 1000000000) {
                until.tv_sec++;
                until.tv_nsec -= 1000000000;
            }
            pthread_cond_timedwait(&c->changed, &c->lock, &until);
            next_due = 0;
            continue;
        }
        was_acquiring = 1;
        pthread_mutex_unlock(&c->lock);

        if (s->src->paced) {
            uint64_t period = (uint64_t)(1e9 / img.frame_rate), now = now_ns();

            if (next_due == 0 || now > next_due + period)
                next_due = now;     /* first frame, or fell behind: don't burst */
            sleep_until(next_due);
            next_due += period;
        }
        if (df != st.do_not_fragment) {
            set_dont_fragment(s->fd, st.do_not_fragment);
            df = st.do_not_fragment;
        }

        const uint8_t *frame = s->src->next(s->src, &img, frame_no);
        uint64_t ts = cam_timestamp(c);

        if (frame && send_frame(s, &st, &img, frame, ts) < 0 && s->verbose)
            perror("camd: stream");
        frame_no++;

        pthread_mutex_lock(&c->lock);
        c->frames_sent++;
    }
    pthread_mutex_unlock(&c->lock);
    return NULL;
}

int streamer_start(struct streamer *s, struct camera *cam, struct frame_source *src,
                   int verbose)
{
    int sndbuf = 4 * 1024 * 1024;

    memset(s, 0, sizeof(*s));
    s->cam = cam;
    s->src = src;
    s->verbose = verbose;
    s->fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (s->fd < 0) {
        perror("camd: stream socket");
        return -1;
    }
    setsockopt(s->fd, SOL_SOCKET, SO_SNDBUF, &sndbuf, sizeof(sndbuf));
    if (pthread_create(&s->thread, NULL, stream_thread, s) != 0) {
        close(s->fd);
        return -1;
    }
    return 0;
}

void streamer_stop(struct streamer *s)
{
    pthread_mutex_lock(&s->cam->lock);
    s->stop = 1;
    pthread_cond_broadcast(&s->cam->changed);
    pthread_mutex_unlock(&s->cam->lock);
    pthread_join(s->thread, NULL);
    close(s->fd);
}

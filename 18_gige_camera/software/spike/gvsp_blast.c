/*
 * gvsp_blast.c — M0 throughput spike, transmit side (runs on the DE10-Nano).
 *
 * Streams a synthetic full-resolution frame buffer as GVSP-sized UDP packets
 * and reports throughput, packet rate, and CPU cost. This answers the
 * question behind ADR-001: can Linux userspace on the Cortex-A9 packetize
 * frames at GigE rate, and with which packet size / syscall batching?
 *
 * Each packet is sent scatter-gather (header + pointer into the frame
 * buffer), the same shape a real GVSP streamer has, so the kernel's copy
 * from the frame buffer is part of the measured cost.
 *
 * Usage:
 *   gvsp_blast -d <pc-ip> [-s payload] [-b batch] [-t seconds] [-r mbps] [-j]
 */
#define _GNU_SOURCE
#include <arpa/inet.h>
#include <errno.h>
#include <getopt.h>
#include <netinet/in.h>
#include <signal.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <unistd.h>

#include "spike_common.h"

#define MAX_BATCH           256
#define MAX_UDP_PAYLOAD     65507
#define DEFAULT_PAYLOAD     1472                 /* fills a 1500-byte MTU */
#define DEFAULT_FRAME_BYTES (2592u * 1944u)      /* full-res BayerGR8 */

struct opts {
    const char *dest;
    int         port;
    int         payload;
    double      duration_s;
    double      rate_mbps;     /* 0 = unlimited */
    int         batch;
    size_t      frame_bytes;
    int         sndbuf;
    int         json;
};

/* Position of the next packet in the frame stream. */
struct cursor {
    uint32_t seq;
    uint32_t frame_id;
    uint32_t packet_id;
    size_t   offset;
};

static volatile sig_atomic_t g_stop;

static void on_signal(int sig)
{
    (void)sig;
    g_stop = 1;
}

static void usage(const char *argv0)
{
    fprintf(stderr,
            "Usage: %s -d <dest-ip> [options]\n"
            "  -d IP      destination (the PC running gvsp_sink)\n"
            "  -p PORT    UDP port (default %d)\n"
            "  -s BYTES   UDP payload per packet incl. %d-byte header (default %d)\n"
            "  -b N       packets per sendmmsg() call, 1..%d (default 1)\n"
            "  -t SEC     duration (default 10)\n"
            "  -r MBPS    pace to this UDP payload rate, 0 = unlimited (default 0)\n"
            "  -f BYTES   synthetic frame size (default %u = 2592x1944 Bayer8)\n"
            "  -B BYTES   SO_SNDBUF (default 4194304)\n"
            "  -j         print a JSON summary line on stdout\n",
            argv0, SPIKE_DEFAULT_PORT, SPIKE_HDR_LEN, DEFAULT_PAYLOAD,
            MAX_BATCH, DEFAULT_FRAME_BYTES);
}

static int parse_opts(int argc, char **argv, struct opts *o)
{
    int c;

    *o = (struct opts){
        .port = SPIKE_DEFAULT_PORT, .payload = DEFAULT_PAYLOAD,
        .duration_s = 10.0, .batch = 1, .frame_bytes = DEFAULT_FRAME_BYTES,
        .sndbuf = 4 * 1024 * 1024,
    };
    while ((c = getopt(argc, argv, "d:p:s:b:t:r:f:B:jh")) != -1) {
        switch (c) {
        case 'd': o->dest = optarg; break;
        case 'p': o->port = atoi(optarg); break;
        case 's': o->payload = atoi(optarg); break;
        case 'b': o->batch = atoi(optarg); break;
        case 't': o->duration_s = atof(optarg); break;
        case 'r': o->rate_mbps = atof(optarg); break;
        case 'f': o->frame_bytes = strtoul(optarg, NULL, 0); break;
        case 'B': o->sndbuf = atoi(optarg); break;
        case 'j': o->json = 1; break;
        default:  return -1;
        }
    }
    if (!o->dest) {
        fprintf(stderr, "error: -d <dest-ip> is required\n");
        return -1;
    }
    if (o->payload <= SPIKE_HDR_LEN || o->payload > MAX_UDP_PAYLOAD) {
        fprintf(stderr, "error: payload must be %d..%d bytes\n",
                SPIKE_HDR_LEN + 1, MAX_UDP_PAYLOAD);
        return -1;
    }
    if (o->batch < 1 || o->batch > MAX_BATCH) {
        fprintf(stderr, "error: batch must be 1..%d\n", MAX_BATCH);
        return -1;
    }
    if (o->frame_bytes == 0 || o->duration_s <= 0 || o->rate_mbps < 0) {
        fprintf(stderr, "error: frame size and duration must be > 0, rate >= 0\n");
        return -1;
    }
    return 0;
}

static size_t chunk_len(const struct cursor *c, size_t data_per_pkt,
                        size_t frame_bytes)
{
    size_t left = frame_bytes - c->offset;

    return left < data_per_pkt ? left : data_per_pkt;
}

static void cursor_advance(struct cursor *c, size_t len, size_t frame_bytes)
{
    c->seq++;
    c->offset += len;
    if (c->offset >= frame_bytes) {
        c->offset = 0;
        c->packet_id = 0;
        c->frame_id++;
    } else {
        c->packet_id++;
    }
}

static int open_socket(const struct opts *o)
{
    struct sockaddr_in dst = { .sin_family = AF_INET, .sin_port = htons(o->port) };
    int pmtu = IP_PMTUDISC_DO;   /* fail with EMSGSIZE instead of fragmenting */
    int fd;

    if (inet_pton(AF_INET, o->dest, &dst.sin_addr) != 1) {
        fprintf(stderr, "error: invalid IPv4 address '%s'\n", o->dest);
        return -1;
    }
    fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        perror("socket");
        return -1;
    }
    setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &o->sndbuf, sizeof(o->sndbuf));
    setsockopt(fd, IPPROTO_IP, IP_MTU_DISCOVER, &pmtu, sizeof(pmtu));
    if (connect(fd, (struct sockaddr *)&dst, sizeof(dst)) < 0) {
        perror("connect");
        close(fd);
        return -1;
    }
    return fd;
}

int main(int argc, char **argv)
{
    static struct mmsghdr   msgs[MAX_BATCH];
    static struct iovec     iov[MAX_BATCH][2];
    static struct spike_hdr hdrs[MAX_BATCH];
    struct opts o;
    struct cursor cur = {0};
    struct cpu_sample cpu0, cpu1;
    struct rusage ru;
    uint64_t packets = 0, bytes = 0, enobufs = 0, refused = 0;
    uint64_t iv_packets = 0, iv_bytes = 0;
    uint64_t t_start, t_end, t_iv, t_now;
    size_t data_per_pkt;
    unsigned char *frame;
    double elapsed, cpu_proc;
    int fd;

    if (parse_opts(argc, argv, &o) < 0) {
        usage(argv[0]);
        return 2;
    }
    data_per_pkt = (size_t)o.payload - SPIKE_HDR_LEN;

    /* Touch every page so first-access faults are not part of the measurement. */
    frame = malloc(o.frame_bytes);
    if (!frame) {
        perror("malloc");
        return 1;
    }
    for (size_t i = 0; i < o.frame_bytes; i++)
        frame[i] = (unsigned char)i;

    fd = open_socket(&o);
    if (fd < 0)
        return 1;

    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);

    fprintf(stderr, "gvsp_blast: %s:%d payload=%d batch=%d frame=%zu B "
            "(%zu packets/frame) rate=%s\n",
            o.dest, o.port, o.payload, o.batch, o.frame_bytes,
            (o.frame_bytes + data_per_pkt - 1) / data_per_pkt,
            o.rate_mbps > 0 ? "paced" : "unlimited");

    cpu_sample_read(&cpu0);
    t_start = t_iv = now_ns();
    t_end = t_start + (uint64_t)(o.duration_s * 1e9);

    while (!g_stop && (t_now = now_ns()) < t_end) {
        struct cursor next = cur;
        int n;

        for (int i = 0; i < o.batch; i++) {
            size_t len = chunk_len(&next, data_per_pkt, o.frame_bytes);

            hdrs[i] = (struct spike_hdr){
                htonl(SPIKE_MAGIC), htonl(next.seq),
                htonl(next.frame_id), htonl(next.packet_id),
            };
            iov[i][0] = (struct iovec){ &hdrs[i], SPIKE_HDR_LEN };
            iov[i][1] = (struct iovec){ frame + next.offset, len };
            msgs[i].msg_hdr = (struct msghdr){ .msg_iov = iov[i], .msg_iovlen = 2 };
            cursor_advance(&next, len, o.frame_bytes);
        }

        n = sendmmsg(fd, msgs, o.batch, 0);
        if (n < 0) {
            if (errno == EINTR)
                continue;
            if (errno == ENOBUFS || errno == EAGAIN) {
                enobufs++;
                continue;
            }
            if (errno == ECONNREFUSED) {   /* ICMP port unreachable: sink not up */
                refused++;
                continue;
            }
            if (errno == EMSGSIZE) {
                fprintf(stderr, "error: %d-byte payload exceeds the path MTU; "
                        "set MTU >= %d on both ends\n", o.payload, o.payload + 28);
                return 3;
            }
            perror("sendmmsg");
            return 1;
        }

        for (int i = 0; i < n; i++) {
            size_t len = iov[i][1].iov_len;

            bytes += SPIKE_HDR_LEN + len;
            iv_bytes += SPIKE_HDR_LEN + len;
            cursor_advance(&cur, len, o.frame_bytes);
        }
        packets += n;
        iv_packets += n;

        if (o.rate_mbps > 0) {
            uint64_t due = t_start + (uint64_t)((double)bytes * 8.0 /
                                                (o.rate_mbps * 1e6) * 1e9);

            while (!g_stop && (t_now = now_ns()) < due) {
                if (due - t_now > 200000) {
                    struct timespec ts = { 0, (long)(due - t_now - 100000) };
                    nanosleep(&ts, NULL);
                }
            }
        }

        if (t_now - t_iv >= 1000000000ull) {
            double dt = (double)(t_now - t_iv) / 1e9;

            fprintf(stderr, "  %6.1f s  %7.1f Mbit/s payload  %8.0f pkt/s\n",
                    (double)(t_now - t_start) / 1e9,
                    (double)iv_bytes * 8.0 / dt / 1e6, (double)iv_packets / dt);
            iv_packets = iv_bytes = 0;
            t_iv = t_now;
        }
    }

    elapsed = (double)(now_ns() - t_start) / 1e9;
    cpu_sample_read(&cpu1);
    getrusage(RUSAGE_SELF, &ru);
    cpu_proc = (double)(ru.ru_utime.tv_sec + ru.ru_stime.tv_sec) +
               (double)(ru.ru_utime.tv_usec + ru.ru_stime.tv_usec) / 1e6;

    fprintf(stderr, "gvsp_blast: %llu packets in %.2f s: %.1f Mbit/s payload, "
            "%.1f Mbit/s on wire, %.0f pkt/s, %.2f frames/s, "
            "process CPU %.0f%%, ENOBUFS %llu\n",
            (unsigned long long)packets, elapsed,
            (double)bytes * 8.0 / elapsed / 1e6,
            (double)(bytes + packets * SPIKE_WIRE_OVERHEAD) * 8.0 / elapsed / 1e6,
            (double)packets / elapsed, (double)cur.frame_id / elapsed,
            100.0 * cpu_proc / elapsed, (unsigned long long)enobufs);

    if (o.json) {
        printf("{\"tool\":\"gvsp_blast\",\"payload\":%d,\"batch\":%d,"
               "\"frame_bytes\":%zu,\"rate_limit_mbps\":%.1f,\"duration_s\":%.3f,"
               "\"packets\":%llu,\"bytes\":%llu,\"frames\":%u,"
               "\"payload_mbps\":%.2f,\"wire_mbps\":%.2f,\"pps\":%.0f,"
               "\"fps\":%.3f,\"enobufs\":%llu,\"refused\":%llu,"
               "\"proc_cpu_pct\":%.1f,",
               o.payload, o.batch, o.frame_bytes, o.rate_mbps, elapsed,
               (unsigned long long)packets, (unsigned long long)bytes,
               cur.frame_id,
               (double)bytes * 8.0 / elapsed / 1e6,
               (double)(bytes + packets * SPIKE_WIRE_OVERHEAD) * 8.0 / elapsed / 1e6,
               (double)packets / elapsed, (double)cur.frame_id / elapsed,
               (unsigned long long)enobufs, (unsigned long long)refused,
               100.0 * cpu_proc / elapsed);
        cpu_json(stdout, &cpu0, &cpu1);
        printf("}\n");
    }

    close(fd);
    free(frame);
    return 0;
}

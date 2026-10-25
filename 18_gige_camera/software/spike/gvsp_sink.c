/*
 * gvsp_sink.c — M0 throughput spike, receive side (runs on the PC).
 *
 * Receives gvsp_blast packets and reports received rate, lost packets
 * (gaps in the sequence number), and reordering. The measurement window
 * runs from the first to the last packet received, so starting the sink
 * early does not dilute the numbers.
 *
 * Usage:
 *   gvsp_sink [-p port] [-i idle-s] [-w wait-s] [-B rcvbuf] [-j]
 */
#define _GNU_SOURCE
#include <arpa/inet.h>
#include <errno.h>
#include <getopt.h>
#include <netinet/in.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <unistd.h>

#include "spike_common.h"

#define BATCH   64
#define BUF_LEN 65536

struct opts {
    int    port;
    double idle_s;     /* stop this long after the last packet */
    double wait_s;     /* give up if nothing arrives within this time */
    int    rcvbuf;
    int    json;
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
            "Usage: %s [options]\n"
            "  -p PORT    UDP port (default %d)\n"
            "  -i SEC     stop after this much silence once packets arrived (default 2)\n"
            "  -w SEC     give up if no packet arrives within this time (default 30)\n"
            "  -B BYTES   SO_RCVBUF (default 33554432)\n"
            "  -j         print a JSON summary line on stdout\n",
            argv0, SPIKE_DEFAULT_PORT);
}

static int parse_opts(int argc, char **argv, struct opts *o)
{
    int c;

    *o = (struct opts){
        .port = SPIKE_DEFAULT_PORT, .idle_s = 2.0, .wait_s = 30.0,
        .rcvbuf = 32 * 1024 * 1024,
    };
    while ((c = getopt(argc, argv, "p:i:w:B:jh")) != -1) {
        switch (c) {
        case 'p': o->port = atoi(optarg); break;
        case 'i': o->idle_s = atof(optarg); break;
        case 'w': o->wait_s = atof(optarg); break;
        case 'B': o->rcvbuf = atoi(optarg); break;
        case 'j': o->json = 1; break;
        default:  return -1;
        }
    }
    if (o->idle_s <= 0 || o->wait_s <= 0) {
        fprintf(stderr, "error: -i and -w must be > 0\n");
        return -1;
    }
    return 0;
}

static int open_socket(const struct opts *o)
{
    struct sockaddr_in addr = {
        .sin_family = AF_INET, .sin_port = htons(o->port),
        .sin_addr.s_addr = htonl(INADDR_ANY),
    };
    struct timeval tv = { 0, 100000 };
    int actual = 0;
    socklen_t len = sizeof(actual);
    int fd;

    fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        perror("socket");
        return -1;
    }
    setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &o->rcvbuf, sizeof(o->rcvbuf));
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind");
        close(fd);
        return -1;
    }

    /* Linux reports twice the usable size; anything below the request was clamped. */
    getsockopt(fd, SOL_SOCKET, SO_RCVBUF, &actual, &len);
    if (actual / 2 < o->rcvbuf)
        fprintf(stderr, "warning: SO_RCVBUF clamped to %d bytes; the PC may drop "
                "packets. Raise it with: sudo sysctl -w net.core.rmem_max=%d\n",
                actual / 2, o->rcvbuf);
    return fd;
}

int main(int argc, char **argv)
{
    static struct mmsghdr msgs[BATCH];
    static struct iovec   iov[BATCH];
    struct opts o;
    struct cpu_sample cpu0, cpu1;
    unsigned char *bufs;
    uint64_t packets = 0, bytes = 0, lost = 0, reordered = 0, bad = 0;
    uint64_t iv_packets = 0, iv_bytes = 0;
    uint64_t t_launch, t_first = 0, t_last = 0, t_iv = 0, t_now;
    uint32_t expected = 0, first_frame = 0, last_frame = 0;
    int started = 0;
    double window;
    int fd;

    if (parse_opts(argc, argv, &o) < 0) {
        usage(argv[0]);
        return 2;
    }
    bufs = malloc((size_t)BATCH * BUF_LEN);
    if (!bufs) {
        perror("malloc");
        return 1;
    }
    for (int i = 0; i < BATCH; i++) {
        iov[i] = (struct iovec){ bufs + (size_t)i * BUF_LEN, BUF_LEN };
        msgs[i].msg_hdr = (struct msghdr){ .msg_iov = &iov[i], .msg_iovlen = 1 };
    }

    fd = open_socket(&o);
    if (fd < 0)
        return 1;

    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);
    fprintf(stderr, "gvsp_sink: listening on UDP %d\n", o.port);

    cpu_sample_read(&cpu0);
    t_launch = now_ns();

    while (!g_stop) {
        int n = recvmmsg(fd, msgs, BATCH, MSG_WAITFORONE, NULL);

        t_now = now_ns();
        if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
            perror("recvmmsg");
            return 1;
        }

        for (int i = 0; i < n; i++) {
            const struct spike_hdr *h = (const struct spike_hdr *)iov[i].iov_base;
            uint32_t seq;

            if (msgs[i].msg_len < SPIKE_HDR_LEN || ntohl(h->magic) != SPIKE_MAGIC) {
                bad++;
                continue;
            }
            seq = ntohl(h->seq);
            if (!started) {
                started = 1;
                expected = seq;
                first_frame = ntohl(h->frame_id);
                t_first = t_iv = t_now;
            }
            if (seq == expected) {
                expected++;
            } else if ((int32_t)(seq - expected) > 0) {
                lost += seq - expected;
                expected = seq + 1;
            } else {
                /* Arrived after a later packet: it was counted lost, undo that. */
                reordered++;
                if (lost)
                    lost--;
            }
            last_frame = ntohl(h->frame_id);
            packets++;
            iv_packets++;
            bytes += msgs[i].msg_len;
            iv_bytes += msgs[i].msg_len;
            t_last = t_now;
        }

        if (started && t_now - t_iv >= 1000000000ull) {
            double dt = (double)(t_now - t_iv) / 1e9;

            fprintf(stderr, "  %7.1f Mbit/s payload  %8.0f pkt/s  lost %llu\n",
                    (double)iv_bytes * 8.0 / dt / 1e6, (double)iv_packets / dt,
                    (unsigned long long)lost);
            iv_packets = iv_bytes = 0;
            t_iv = t_now;
        }
        if (started && t_now - t_last > (uint64_t)(o.idle_s * 1e9))
            break;
        if (!started && t_now - t_launch > (uint64_t)(o.wait_s * 1e9)) {
            fprintf(stderr, "gvsp_sink: no packets within %.0f s\n", o.wait_s);
            break;
        }
    }

    cpu_sample_read(&cpu1);
    window = started && t_last > t_first ? (double)(t_last - t_first) / 1e9 : 0.0;

    fprintf(stderr, "gvsp_sink: %llu packets, %llu lost (%.4f%%), %llu reordered, "
            "%llu malformed, %.1f Mbit/s payload over %.2f s\n",
            (unsigned long long)packets, (unsigned long long)lost,
            packets + lost ? 100.0 * (double)lost / (double)(packets + lost) : 0.0,
            (unsigned long long)reordered, (unsigned long long)bad,
            window > 0 ? (double)bytes * 8.0 / window / 1e6 : 0.0, window);

    if (o.json) {
        printf("{\"tool\":\"gvsp_sink\",\"duration_s\":%.3f,\"packets\":%llu,"
               "\"bytes\":%llu,\"lost\":%llu,\"reordered\":%llu,\"malformed\":%llu,"
               "\"loss_pct\":%.5f,\"frames_spanned\":%u,"
               "\"payload_mbps\":%.2f,\"wire_mbps\":%.2f,\"pps\":%.0f,",
               window, (unsigned long long)packets, (unsigned long long)bytes,
               (unsigned long long)lost, (unsigned long long)reordered,
               (unsigned long long)bad,
               packets + lost ? 100.0 * (double)lost / (double)(packets + lost) : 0.0,
               started ? last_frame - first_frame + 1 : 0,
               window > 0 ? (double)bytes * 8.0 / window / 1e6 : 0.0,
               window > 0 ? (double)(bytes + packets * SPIKE_WIRE_OVERHEAD) * 8.0 /
                                window / 1e6 : 0.0,
               window > 0 ? (double)packets / window : 0.0);
        cpu_json(stdout, &cpu0, &cpu1);
        printf("}\n");
    }

    close(fd);
    free(bufs);
    return packets ? 0 : 2;
}

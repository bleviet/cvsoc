/*
 * spike_common.h — shared definitions for the M0 GigE throughput spike
 * (gvsp_blast on the DE10-Nano, gvsp_sink on the PC).
 */
#ifndef SPIKE_COMMON_H
#define SPIKE_COMMON_H

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define SPIKE_MAGIC        0x47565342u   /* "GVSB" */
#define SPIKE_DEFAULT_PORT 50010
#define SPIKE_HDR_LEN      16

/*
 * Bytes per packet on the wire that are not UDP payload:
 * preamble+SFD 8, Ethernet header 14, IPv4 20, UDP 8, FCS 4, inter-frame gap 12.
 */
#define SPIKE_WIRE_OVERHEAD 66

/*
 * Spike packet header, all fields big-endian. It plays the role of the
 * 8-byte GVSP header (block ID + packet ID) and adds a global sequence
 * number so the sink can count loss and reordering exactly.
 */
struct spike_hdr {
    uint32_t magic;
    uint32_t seq;
    uint32_t frame_id;
    uint32_t packet_id;
};

static inline uint64_t now_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

/* ── System-wide CPU accounting from /proc/stat ───────────────────────────── */
/*
 * Process CPU time (getrusage) misses most of the network stack cost, which
 * runs in softirq context, so the spike reports per-core busy time as well.
 * Index 0 is the aggregate "cpu" line, index n+1 is "cpu<n>".
 */
#define SPIKE_MAX_CPUS 8

struct cpu_sample {
    int      ncpu;
    uint64_t busy[SPIKE_MAX_CPUS + 1];
    uint64_t softirq[SPIKE_MAX_CPUS + 1];
    uint64_t total[SPIKE_MAX_CPUS + 1];
};

static inline int cpu_sample_read(struct cpu_sample *s)
{
    FILE *f = fopen("/proc/stat", "r");
    char line[256];

    memset(s, 0, sizeof(*s));
    if (!f)
        return -1;

    while (fgets(line, sizeof(line), f)) {
        char name[16];
        unsigned long long user, nice, sys, idle, iowait, irq, softirq, steal;
        int idx;

        if (strncmp(line, "cpu", 3) != 0)
            break;
        if (sscanf(line, "%15s %llu %llu %llu %llu %llu %llu %llu %llu",
                   name, &user, &nice, &sys, &idle, &iowait, &irq,
                   &softirq, &steal) != 9)
            continue;

        if (name[3] == '\0') {
            idx = 0;
        } else {
            idx = atoi(&name[3]) + 1;
            if (idx > SPIKE_MAX_CPUS)
                continue;
            if (idx > s->ncpu)
                s->ncpu = idx;
        }
        s->total[idx]   = user + nice + sys + idle + iowait + irq + softirq + steal;
        s->busy[idx]    = s->total[idx] - idle - iowait;
        s->softirq[idx] = softirq;
    }
    fclose(f);
    return 0;
}

/* Share of all CPU time between two samples spent in `field` on core `idx`. */
#define CPU_PCT(s0, s1, field, idx)                                           \
    ((s1)->total[idx] > (s0)->total[idx]                                      \
         ? 100.0 * (double)((s1)->field[idx] - (s0)->field[idx]) /            \
               (double)((s1)->total[idx] - (s0)->total[idx])                  \
         : 0.0)

static inline void cpu_json(FILE *out, const struct cpu_sample *s0,
                            const struct cpu_sample *s1)
{
    fprintf(out, "\"cpu_busy_pct\":%.1f,\"cpu_softirq_pct\":%.1f,\"per_core_busy_pct\":[",
            CPU_PCT(s0, s1, busy, 0), CPU_PCT(s0, s1, softirq, 0));
    for (int i = 1; i <= s1->ncpu; i++)
        fprintf(out, "%s%.1f", i > 1 ? "," : "", CPU_PCT(s0, s1, busy, i));
    fputc(']', out);
}

#endif /* SPIKE_COMMON_H */

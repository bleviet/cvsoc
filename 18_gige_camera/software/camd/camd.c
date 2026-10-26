/*
 * camd.c — cvsoc GigE Vision camera daemon.
 *
 * Serves GVCP on UDP 3956 (discovery, registers, GenICam XML, control
 * privilege, heartbeat) and streams frames on GVSP stream channel 0.
 *
 * Usage:
 *   camd --sim  [-p port] [-W width] [-H height] [-P max-packet] [-v]
 *   camd --fpga [--fb-base addr] [--fb-size bytes] [--exposure-us N] [--gain G] [...]
 *   camd --sensor-test
 */
#define _GNU_SOURCE
#include <arpa/inet.h>
#include <getopt.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "camera.h"
#include "gvcp.h"
#include "gvsp.h"
#include "source.h"
#include "util.h"

#ifndef CAMD_VERSION
#define CAMD_VERSION "0.2.0"
#endif

extern const unsigned char camd_xml_start[], camd_xml_end[];

static const char default_serial[] = "CVSOC-SIM";

static volatile sig_atomic_t g_stop;

static void on_signal(int sig)
{
    (void)sig;
    g_stop = 1;
}

static void usage(const char *argv0)
{
    fprintf(stderr,
            "Usage: %s --sim|--fpga [options]\n"
            "       %s --sensor-test\n"
            "  --sim        software test-pattern source (no FPGA)\n"
            "  --fpga       frames from cam_frame_writer in reserved DDR: the D5M\n"
            "               sensor (TestPattern Off) or the FPGA test patterns\n"
            "  --sensor-test  check the D5M data lines with the sensor's test\n"
            "               pattern, then exit\n"
            "  --sensor-reg R[=V]  read or write a sensor register (repeatable),\n"
            "               print the capture statistics, then exit\n"
            "  --fb-base A  frame-buffer region (default 0x3C000000)\n"
            "  --fb-size N  frame-buffer region size (default 0x4000000)\n"
            "  --exposure-us N  sensor exposure (default 20000)\n"
            "  --gain G     sensor analog gain, 1.0 to 15.75 (default 2.0)\n"
            "  -p PORT      GVCP port (default %d)\n"
            "  -W PIXELS    sensor width (default 2592)\n"
            "  -H PIXELS    sensor height (default 1944)\n"
            "  -P BYTES     largest GevSCPSPacketSize accepted (default 3796)\n"
            "  -s SERIAL    serial number (default CVSOC-SIM)\n"
            "  -v           log every GVCP command\n",
            argv0, argv0, GVCP_PORT);
}

int main(int argc, char **argv)
{
    static const struct option longopts[] = {
        { "sim", no_argument, NULL, 'S' },
        { "fpga", no_argument, NULL, 'F' },
        { "fb-base", required_argument, NULL, 'b' },
        { "fb-size", required_argument, NULL, 'z' },
        { "exposure-us", required_argument, NULL, 'e' },
        { "gain", required_argument, NULL, 'g' },
        { "sensor-test", no_argument, NULL, 'T' },
        { "sensor-reg", required_argument, NULL, 'R' },
        { "help", no_argument, NULL, 'h' },
        { 0 },
    };
    static struct camera cam;
    struct cam_identity id = {
        .manufacturer = "cvsoc",
        .model = "DE10-Nano GigE Camera",
        .version = "camd " CAMD_VERSION,
        .info = "M2 sensor bring-up",
        .serial = default_serial,
        .sensor_width = 2592, .sensor_height = 1944,
        .max_packet_size = 3796,
        .xml_name = "cvsoc_camera.xml",
    };
    struct streamer streamer;
    struct frame_source *src;
    uint32_t fb_base = 0x3C000000u, fb_size = 0x4000000u;
    struct sensor_opts sensor = { .exposure_us = 20000, .gain = 2.0 };
    char *regs[64];
    int nregs = 0;
    int port = GVCP_PORT, sim = 0, fpga = 0, verbose = 0, fd, c;

    while ((c = getopt_long(argc, argv, "p:W:H:P:s:vh", longopts, NULL)) != -1) {
        switch (c) {
        case 'S': sim = 1; break;
        case 'F': fpga = 1; break;
        case 'b': fb_base = (uint32_t)strtoul(optarg, NULL, 0); break;
        case 'z': fb_size = (uint32_t)strtoul(optarg, NULL, 0); break;
        case 'e': sensor.exposure_us = atof(optarg); break;
        case 'g': sensor.gain = atof(optarg); break;
        case 'T': return source_fpga_sensor_test();
        case 'R':
            if (nregs < 64)
                regs[nregs++] = optarg;
            break;
        case 'p': port = atoi(optarg); break;
        case 'W': id.sensor_width = (uint32_t)atoi(optarg); break;
        case 'H': id.sensor_height = (uint32_t)atoi(optarg); break;
        case 'P': id.max_packet_size = (uint32_t)atoi(optarg); break;
        case 's': id.serial = optarg; break;
        case 'v': verbose = 1; break;
        default:  usage(argv[0]); return 2;
        }
    }
    if (nregs)
        return source_fpga_sensor_regs(regs, nregs);
    if (sim == fpga) {
        fprintf(stderr, "camd: choose exactly one of --sim and --fpga\n");
        usage(argv[0]);
        return 2;
    }
    if (fpga && id.serial == default_serial)
        id.serial = "CVSOC-FPGA";
    if (id.sensor_width < WIDTH_MIN || id.sensor_width % WIDTH_INC ||
        id.sensor_height < HEIGHT_MIN || id.sensor_height % HEIGHT_INC ||
        id.max_packet_size < PACKET_SIZE_MIN || id.max_packet_size > 9000) {
        fprintf(stderr, "camd: invalid sensor size or packet size\n");
        return 2;
    }

    id.xml = camd_xml_start;
    id.xml_len = (size_t)(camd_xml_end - camd_xml_start);
    cam_init(&cam, &id);

    src = sim ? source_sim_open(id.sensor_width, id.sensor_height)
              : source_fpga_open(id.sensor_width, id.sensor_height, fb_base, fb_size,
                                 &sensor);
    if (!src) {
        fprintf(stderr, "camd: cannot open frame source\n");
        return 1;
    }
    cam_attach_source(&cam, src);
    fd = gvcp_open((uint16_t)port);
    if (fd < 0)
        return 1;
    if (streamer_start(&streamer, &cam, src, verbose) < 0)
        return 1;

    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);
    fprintf(stderr, "camd %s: GVCP on UDP %d, source %s, sensor %ux%u, max packet %u\n",
            CAMD_VERSION, port, src->name, id.sensor_width, id.sensor_height,
            id.max_packet_size);

    while (!g_stop) {
        struct pollfd pfd = { .fd = fd, .events = POLLIN };

        if (poll(&pfd, 1, 100) > 0 && (pfd.revents & POLLIN)) {
            uint8_t buf[GVCP_MAX_PACKET];
            struct sockaddr_in peer;
            socklen_t plen = sizeof(peer);
            ssize_t n = recvfrom(fd, buf, sizeof(buf), 0, (struct sockaddr *)&peer, &plen);

            if (n > 0)
                gvcp_handle(&cam, fd, buf, (size_t)n, &peer, verbose);
        }
        if (cam_heartbeat_check(&cam, now_ns()))
            fprintf(stderr, "camd: heartbeat expired, control released\n");
    }

    streamer_stop(&streamer);
    src->close(src);
    close(fd);
    return 0;
}

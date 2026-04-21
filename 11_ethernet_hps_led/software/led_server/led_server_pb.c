/*
 * led_server_pb.c — UDP server with protobuf (nanopb) wire encoding.
 *
 * Phase 7.5: Protocol Buffers extension.
 *
 * This is a drop-in replacement for led_server.c with an identical LED
 * hardware interface (mmap /dev/mem at LWH2F_BASE) but a different wire
 * protocol: instead of the raw 2-byte request/response, each packet is a
 * nanopb-encoded LedCommand / LedResponse message as defined in:
 *
 *   proto/led_command.proto
 *
 * Default port is 5006 (so both servers can run side-by-side on the same
 * board for comparison; use --port to override).
 *
 * Wire protocol (Phase 7.5 protobuf):
 *
 *   Request:  nanopb-encoded LedCommand  { command, pattern }
 *   Response: nanopb-encoded LedResponse { status, pattern }
 *
 *   Maximum encoded sizes (from led_command.pb.h):
 *     LedCommand  ≤ 8 bytes
 *     LedResponse ≤ 8 bytes
 *
 * Build (standalone cross-compile, requires nanopb runtime on target):
 *   arm-linux-gnueabihf-gcc -O2 -Wall -Wextra \
 *       -I$(NANOPB_DIR) \
 *       -o led_server_pb \
 *       led_server_pb.c led_command.pb.c $(NANOPB_DIR)/pb_encode.c \
 *       $(NANOPB_DIR)/pb_decode.c $(NANOPB_DIR)/pb_common.c
 *
 * Or simply: make server-pb-cross  (from 11_ethernet_hps_led/)
 *
 * Usage:
 *   led_server_pb                  # listen on 0.0.0.0:5006
 *   led_server_pb --port 5007      # use a different port
 *   led_server_pb --help
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <errno.h>

/* nanopb runtime headers */
#include <pb_encode.h>
#include <pb_decode.h>

/* Generated message descriptors */
#include "led_command.pb.h"

/* ── Protocol constants ───────────────────────────────────────────────────── */
#define LED_PB_SERVER_PORT   5006

/* ── Hardware constants ───────────────────────────────────────────────────── */
#define LWH2F_BASE      0xFF200000UL
#define MAP_SIZE        0x1000UL
#define PIO_DATA_OFFSET 0x00

/* Receive buffer: must be ≥ led_LedCommand_size (8 bytes); use 128 for margin */
#define RX_BUF_SIZE  128
/* Transmit buffer: must be ≥ led_LedResponse_size (8 bytes) */
#define TX_BUF_SIZE  128

static volatile int running = 1;

static void signal_handler(int sig)
{
    (void)sig;
    running = 0;
}

static void usage(const char *progname)
{
    printf("Usage: %s [OPTIONS]\n\n", progname);
    printf("UDP/protobuf server that controls FPGA LEDs on the DE10-Nano.\n\n");
    printf("Protocol: nanopb-encoded LedCommand / LedResponse (Phase 7.5)\n");
    printf("Proto:    proto/led_command.proto\n\n");
    printf("Options:\n");
    printf("  -p, --port PORT    UDP listen port (default: %d)\n", LED_PB_SERVER_PORT);
    printf("  -h, --help         Show this help message\n\n");
    printf("Use alongside led_server (port 5005) for side-by-side comparison:\n");
    printf("  led_server    --port 5005   # raw 2-byte protocol\n");
    printf("  led_server_pb --port 5006   # nanopb protobuf protocol\n");
}

static inline void led_write(volatile uint32_t *base, uint8_t value)
{
    *base = (uint32_t)value;
}

static inline uint8_t led_read(const volatile uint32_t *base)
{
    return (uint8_t)(*base);
}

/*
 * Send an error response back to the client.
 * Used when the incoming packet cannot be decoded.
 */
static void send_error_response(int sockfd, struct sockaddr_in6 *client,
                                socklen_t client_len,
                                led_StatusCode status, uint8_t current_led)
{
    uint8_t tx[TX_BUF_SIZE];
    led_LedResponse resp = led_LedResponse_init_zero;
    resp.status  = status;
    resp.pattern = (uint32_t)current_led;

    pb_ostream_t out = pb_ostream_from_buffer(tx, sizeof(tx));
    if (pb_encode(&out, led_LedResponse_fields, &resp)) {
        sendto(sockfd, tx, out.bytes_written, 0,
               (struct sockaddr *)client, client_len);
    }
}

int main(int argc, char *argv[])
{
    int port = LED_PB_SERVER_PORT;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            usage(argv[0]);
            return 0;
        } else if ((strcmp(argv[i], "-p") == 0 ||
                    strcmp(argv[i], "--port") == 0) && i + 1 < argc) {
            port = atoi(argv[++i]);
            if (port <= 0 || port > 65535) {
                fprintf(stderr, "Error: invalid port: %s\n", argv[i]);
                return 1;
            }
        } else {
            fprintf(stderr, "Error: unknown argument: %s\n", argv[i]);
            usage(argv[0]);
            return 1;
        }
    }

    /* ── Open and map /dev/mem ────────────────────────────────────────────── */
    int memfd = open("/dev/mem", O_RDWR | O_SYNC);
    if (memfd < 0) {
        fprintf(stderr, "Error: cannot open /dev/mem: %s\n", strerror(errno));
        fprintf(stderr, "Hint: run as root.\n");
        return 1;
    }

    volatile uint32_t *led_base = (volatile uint32_t *)mmap(
        NULL, MAP_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, memfd, LWH2F_BASE
    );
    if (led_base == MAP_FAILED) {
        fprintf(stderr, "Error: mmap failed: %s\n", strerror(errno));
        close(memfd);
        return 1;
    }

    /* ── Create dual-stack UDP socket ────────────────────────────────────── */
    int sockfd = socket(AF_INET6, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        fprintf(stderr, "Error: socket: %s\n", strerror(errno));
        munmap((void *)led_base, MAP_SIZE);
        close(memfd);
        return 1;
    }

    int opt = 1;
    setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    /* Dual-stack: also accept IPv4-mapped (::ffff:x.x.x.x) clients */
    int v6only = 0;
    setsockopt(sockfd, IPPROTO_IPV6, IPV6_V6ONLY, &v6only, sizeof(v6only));

    struct sockaddr_in6 srv = {
        .sin6_family = AF_INET6,
        .sin6_port   = htons((uint16_t)port),
        .sin6_addr   = in6addr_any,
    };
    if (bind(sockfd, (struct sockaddr *)&srv, sizeof(srv)) < 0) {
        fprintf(stderr, "Error: bind to port %d: %s\n", port, strerror(errno));
        close(sockfd);
        munmap((void *)led_base, MAP_SIZE);
        close(memfd);
        return 1;
    }

    signal(SIGINT,  signal_handler);
    signal(SIGTERM, signal_handler);

    printf("LED protobuf server listening on UDP port %d\n", port);
    printf("LED PIO at 0x%08lX (via /dev/mem)\n", (unsigned long)LWH2F_BASE);
    printf("Protocol: nanopb LedCommand / LedResponse (Phase 7.5)\n");
    printf("Initial LED state: 0x%02X\n", led_read(led_base));
    printf("Press Ctrl+C to stop.\n\n");

    /* ── Main receive loop ────────────────────────────────────────────────── */
    uint8_t rx[RX_BUF_SIZE];
    uint8_t tx[TX_BUF_SIZE];
    struct sockaddr_in6 client;
    socklen_t client_len = sizeof(client);

    while (running) {
        ssize_t nbytes = recvfrom(
            sockfd, rx, sizeof(rx), 0,
            (struct sockaddr *)&client, &client_len
        );

        if (nbytes < 0) {
            if (errno == EINTR) break;
            fprintf(stderr, "recvfrom: %s\n", strerror(errno));
            continue;
        }

        char client_ip[INET6_ADDRSTRLEN];
        inet_ntop(AF_INET6, &client.sin6_addr, client_ip, sizeof(client_ip));

        /* ── Decode incoming LedCommand ─────────────────────────────────── */
        led_LedCommand cmd = led_LedCommand_init_zero;
        pb_istream_t in = pb_istream_from_buffer(rx, (size_t)nbytes);

        if (!pb_decode(&in, led_LedCommand_fields, &cmd)) {
            fprintf(stderr, "Decode error from %s: %s\n",
                    client_ip, PB_GET_ERROR(&in));
            send_error_response(sockfd, &client, client_len,
                                led_StatusCode_ERR_DECODE_FAIL,
                                led_read(led_base));
            continue;
        }

        /* ── Process command ─────────────────────────────────────────────── */
        led_LedResponse resp = led_LedResponse_init_zero;

        switch (cmd.command) {
        case led_CommandType_SET_PATTERN:
            led_write(led_base, (uint8_t)(cmd.pattern & 0xFF));
            resp.status  = led_StatusCode_OK;
            resp.pattern = (uint32_t)led_read(led_base);
            printf("SET 0x%02X  from %-40s → LED=0x%02X\n",
                   cmd.pattern & 0xFF, client_ip, resp.pattern);
            break;

        case led_CommandType_GET_PATTERN:
            resp.status  = led_StatusCode_OK;
            resp.pattern = (uint32_t)led_read(led_base);
            printf("GET       from %-40s → LED=0x%02X\n",
                   client_ip, resp.pattern);
            break;

        default:
            fprintf(stderr, "Unknown command %d from %s\n",
                    (int)cmd.command, client_ip);
            resp.status  = led_StatusCode_ERR_UNKNOWN_CMD;
            resp.pattern = (uint32_t)led_read(led_base);
            break;
        }

        /* ── Encode and send LedResponse ─────────────────────────────────── */
        pb_ostream_t out = pb_ostream_from_buffer(tx, sizeof(tx));
        if (!pb_encode(&out, led_LedResponse_fields, &resp)) {
            fprintf(stderr, "Encode error: %s\n", PB_GET_ERROR(&out));
            continue;
        }

        sendto(sockfd, tx, out.bytes_written, 0,
               (struct sockaddr *)&client, client_len);
    }

    printf("\nShutting down. Turning off LEDs.\n");
    led_write(led_base, 0x00);

    close(sockfd);
    munmap((void *)led_base, MAP_SIZE);
    close(memfd);
    return 0;
}

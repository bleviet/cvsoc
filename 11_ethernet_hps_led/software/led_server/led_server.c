/*
 * led_server.c — UDP server for remote FPGA LED control on DE10-Nano.
 *
 * Phase 7 Track A: HPS Ethernet
 *
 * Listens for UDP packets on LED_SERVER_PORT (default 5005) and controls the
 * 8 on-board LEDs through the FPGA LED PIO peripheral via /dev/mem mmap at
 * the LW H2F bridge address (0xFF200000).
 *
 * Wire protocol (Phase 7 raw UDP baseline):
 *
 *   Request  (2 bytes): [CMD] [PATTERN]
 *   Response (2 bytes): [STATUS] [PATTERN]
 *
 *   CMD values:
 *     0x01  SET_PATTERN  Write PATTERN to LED PIO register
 *     0x02  GET_PATTERN  Read current LED PIO register value (PATTERN ignored)
 *
 *   STATUS values:
 *     0x00  OK
 *     0x01  ERR_UNKNOWN_CMD
 *     0x02  ERR_INVALID_LENGTH
 *
 * Note: §7.5 of the roadmap replaces this raw byte protocol with a protobuf
 * (nanopb) encoding while keeping the UDP transport and LED logic unchanged.
 *
 * Usage:
 *   led_server                     # listen on 0.0.0.0:5005
 *   led_server --port 6000         # use a different port
 *   led_server --help
 *
 * Build (cross-compile):
 *   arm-linux-gnueabihf-gcc -O2 -Wall -Wextra -o led_server led_server.c
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

/* ── Protocol constants ───────────────────────────────────────────────────── */
#define LED_SERVER_PORT    5005

#define CMD_SET_PATTERN    0x01
#define CMD_GET_PATTERN    0x02

#define STATUS_OK               0x00
#define STATUS_ERR_UNKNOWN_CMD  0x01
#define STATUS_ERR_INVALID_LEN  0x02

#define REQUEST_LEN     2
#define RESPONSE_LEN    2

/* ── Hardware constants ───────────────────────────────────────────────────── */
/* LW H2F bridge physical base address on Cyclone V SoC */
#define LWH2F_BASE       0xFF200000UL
#define MAP_SIZE         0x1000UL

/* LED PIO DATA register offset (Altera Avalon PIO) */
#define PIO_DATA_OFFSET  0x00

static volatile int running = 1;

static void signal_handler(int sig)
{
    (void)sig;
    running = 0;
}

static void usage(const char *progname)
{
    printf("Usage: %s [OPTIONS]\n\n", progname);
    printf("UDP server that controls FPGA LEDs on the DE10-Nano via /dev/mem.\n\n");
    printf("Options:\n");
    printf("  -p, --port PORT    UDP listen port (default: %d)\n", LED_SERVER_PORT);
    printf("  -h, --help         Show this help message\n\n");
    printf("Protocol:\n");
    printf("  Request  (2 bytes): [CMD] [PATTERN]\n");
    printf("  Response (2 bytes): [STATUS] [CURRENT_PATTERN]\n\n");
    printf("  CMD 0x01 = SET_PATTERN  write PATTERN to LED register\n");
    printf("  CMD 0x02 = GET_PATTERN  read current LED register value\n\n");
    printf("Example (from PC):\n");
    printf("  python3 send_led_pattern.py --host <board-ip> --pattern 0xA5\n");
}

static inline void led_write(volatile uint32_t *base, uint8_t value)
{
    *(base + (PIO_DATA_OFFSET / sizeof(uint32_t))) = (uint32_t)value;
}

static inline uint8_t led_read(volatile uint32_t *base)
{
    return (uint8_t)(*(base + (PIO_DATA_OFFSET / sizeof(uint32_t))));
}

int main(int argc, char *argv[])
{
    int port = LED_SERVER_PORT;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            usage(argv[0]);
            return 0;
        } else if ((strcmp(argv[i], "-p") == 0 || strcmp(argv[i], "--port") == 0) && i + 1 < argc) {
            port = atoi(argv[++i]);
            if (port <= 0 || port > 65535) {
                fprintf(stderr, "Error: invalid port number: %s\n", argv[i]);
                return 1;
            }
        } else {
            fprintf(stderr, "Error: unknown argument: %s\n", argv[i]);
            usage(argv[0]);
            return 1;
        }
    }

    /* ── Open and map /dev/mem for LED PIO access ─────────────────────────── */
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

    /* ── Create UDP socket (dual-stack IPv6 + IPv4-mapped) ───────────────── */
    int sockfd = socket(AF_INET6, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        fprintf(stderr, "Error: socket creation failed: %s\n", strerror(errno));
        munmap((void *)led_base, MAP_SIZE);
        close(memfd);
        return 1;
    }

    /* Allow address reuse so the server can restart quickly */
    int opt = 1;
    setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    /* Dual-stack: accept both IPv6 and IPv4-mapped (::ffff:x.x.x.x) */
    int v6only = 0;
    setsockopt(sockfd, IPPROTO_IPV6, IPV6_V6ONLY, &v6only, sizeof(v6only));

    struct sockaddr_in6 server_addr = {
        .sin6_family = AF_INET6,
        .sin6_port   = htons((uint16_t)port),
        .sin6_addr   = in6addr_any,
    };

    if (bind(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        fprintf(stderr, "Error: bind to port %d failed: %s\n", port, strerror(errno));
        close(sockfd);
        munmap((void *)led_base, MAP_SIZE);
        close(memfd);
        return 1;
    }

    signal(SIGINT,  signal_handler);
    signal(SIGTERM, signal_handler);

    printf("LED server listening on UDP port %d\n", port);
    printf("LED PIO at 0x%08lX (via /dev/mem)\n", (unsigned long)LWH2F_BASE);
    printf("Initial LED state: 0x%02X\n", led_read(led_base));
    printf("Press Ctrl+C to stop.\n\n");

    /* ── Main receive loop ────────────────────────────────────────────────── */
    uint8_t  req[REQUEST_LEN];
    uint8_t  resp[RESPONSE_LEN];
    struct sockaddr_in6 client_addr;
    socklen_t client_len = sizeof(client_addr);

    while (running) {
        ssize_t nbytes = recvfrom(
            sockfd, req, sizeof(req), 0,
            (struct sockaddr *)&client_addr, &client_len
        );

        if (nbytes < 0) {
            if (errno == EINTR)
                break;  /* SIGINT/SIGTERM */
            fprintf(stderr, "recvfrom error: %s\n", strerror(errno));
            continue;
        }

        char client_ip[INET6_ADDRSTRLEN];
        inet_ntop(AF_INET6, &client_addr.sin6_addr, client_ip, sizeof(client_ip));

        if (nbytes != REQUEST_LEN) {
            fprintf(stderr, "Invalid request length %zd from %s (expected %d)\n",
                    nbytes, client_ip, REQUEST_LEN);
            resp[0] = STATUS_ERR_INVALID_LEN;
            resp[1] = led_read(led_base);
            sendto(sockfd, resp, RESPONSE_LEN, 0,
                   (struct sockaddr *)&client_addr, client_len);
            continue;
        }

        uint8_t cmd     = req[0];
        uint8_t pattern = req[1];

        switch (cmd) {
        case CMD_SET_PATTERN:
            led_write(led_base, pattern);
            resp[0] = STATUS_OK;
            resp[1] = led_read(led_base);
            printf("SET 0x%02X  from %s → LED=0x%02X\n",
                   pattern, client_ip, resp[1]);
            break;

        case CMD_GET_PATTERN:
            resp[0] = STATUS_OK;
            resp[1] = led_read(led_base);
            printf("GET       from %s → LED=0x%02X\n",
                   client_ip, resp[1]);
            break;

        default:
            fprintf(stderr, "Unknown CMD 0x%02X from %s\n", cmd, client_ip);
            resp[0] = STATUS_ERR_UNKNOWN_CMD;
            resp[1] = led_read(led_base);
            break;
        }

        sendto(sockfd, resp, RESPONSE_LEN, 0,
               (struct sockaddr *)&client_addr, client_len);
    }

    printf("\nShutting down. Turning off LEDs.\n");
    led_write(led_base, 0x00);

    close(sockfd);
    munmap((void *)led_base, MAP_SIZE);
    close(memfd);
    return 0;
}

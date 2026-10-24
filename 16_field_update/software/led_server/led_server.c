/*
 * led_server.c — Versioned UDP server for remote FPGA LED control on DE10-Nano.
 *
 * Phase 10 (16_field_update): Network Field Update & Automatic Rollback
 *
 * Extends the Phase 7 UDP server with:
 *   - Compile-time version string (APP_VERSION_STR) and startup LED pattern
 *     (DEFAULT_STARTUP_PATTERN) so v1.0.0 (0x0F) and v2.0.0 (0xA5) are
 *     immediately distinguishable on the physical board LEDs at boot/update.
 *   - --version CLI flag for manifest/health verification.
 *   - --mock-mem <file> option for host-side testing without /dev/mem.
 *   - Compile-time SIMULATE_CRASH_ON_START (or --crash-on-start flag) to test
 *     automatic rollback when a buggy firmware update crashes on startup.
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

#ifndef APP_VERSION_STR
#define APP_VERSION_STR "1.0.0"
#endif

#ifndef DEFAULT_STARTUP_PATTERN
#define DEFAULT_STARTUP_PATTERN 0x0F
#endif

#ifndef SIMULATE_CRASH_ON_START
#define SIMULATE_CRASH_ON_START 0
#endif

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
#define LWH2F_BASE       0xFF200000UL
#define MAP_SIZE         0x1000UL
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
    printf("UDP server (%s) that controls FPGA LEDs on the DE10-Nano.\n\n",
           APP_VERSION_STR);
    printf("Options:\n");
    printf("  -p, --port PORT          UDP listen port (default: %d)\n", LED_SERVER_PORT);
    printf("  -i, --init-pattern HEX   Initial LED pattern on start (default: 0x%02X)\n",
           DEFAULT_STARTUP_PATTERN);
    printf("  -m, --mock-mem FILE      Use a regular file instead of /dev/mem (for testing)\n");
    printf("      --crash-on-start     Simulate a fatal startup crash (rollback testing)\n");
    printf("  -v, --version            Print version string and exit\n");
    printf("  -h, --help               Show this help message\n");
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
    uint8_t init_pattern = (uint8_t)DEFAULT_STARTUP_PATTERN;
    const char *mock_mem_path = NULL;
    int crash_on_start = SIMULATE_CRASH_ON_START;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            usage(argv[0]);
            return 0;
        } else if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--version") == 0) {
            printf("%s\n", APP_VERSION_STR);
            return 0;
        } else if (strcmp(argv[i], "--crash-on-start") == 0) {
            crash_on_start = 1;
        } else if ((strcmp(argv[i], "-p") == 0 || strcmp(argv[i], "--port") == 0) && i + 1 < argc) {
            port = atoi(argv[++i]);
            if (port <= 0 || port > 65535) {
                fprintf(stderr, "Error: invalid port number: %s\n", argv[i]);
                return 1;
            }
        } else if ((strcmp(argv[i], "-i") == 0 || strcmp(argv[i], "--init-pattern") == 0) && i + 1 < argc) {
            init_pattern = (uint8_t)strtoul(argv[++i], NULL, 0);
        } else if ((strcmp(argv[i], "-m") == 0 || strcmp(argv[i], "--mock-mem") == 0) && i + 1 < argc) {
            mock_mem_path = argv[++i];
        } else {
            fprintf(stderr, "Error: unknown argument: %s\n", argv[i]);
            usage(argv[0]);
            return 1;
        }
    }

    if (crash_on_start) {
        fprintf(stderr, "FATAL: simulated startup fault in led_server %s!\n",
                APP_VERSION_STR);
        return 1;
    }

    /* ── Open and map /dev/mem (or mock file) for LED PIO access ─────────── */
    int memfd = -1;
    off_t map_offset = (off_t)LWH2F_BASE;

    if (mock_mem_path != NULL) {
        memfd = open(mock_mem_path, O_RDWR | O_CREAT, 0600);
        if (memfd < 0) {
            fprintf(stderr, "Error: cannot open mock mem %s: %s\n",
                    mock_mem_path, strerror(errno));
            return 1;
        }
        if (ftruncate(memfd, (off_t)MAP_SIZE) < 0) {
            fprintf(stderr, "Error: ftruncate failed: %s\n", strerror(errno));
            close(memfd);
            return 1;
        }
        map_offset = 0;
    } else {
        memfd = open("/dev/mem", O_RDWR | O_SYNC);
        if (memfd < 0) {
            fprintf(stderr, "Error: cannot open /dev/mem: %s\n", strerror(errno));
            return 1;
        }
    }

    volatile uint32_t *led_base = (volatile uint32_t *)mmap(
        NULL, MAP_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, memfd, map_offset
    );
    if (led_base == MAP_FAILED) {
        fprintf(stderr, "Error: mmap failed: %s\n", strerror(errno));
        close(memfd);
        return 1;
    }

    /* Set initial startup LED pattern immediately so version is visible */
    led_write(led_base, init_pattern);

    /* ── Create UDP socket (dual-stack IPv6 + IPv4-mapped) ───────────────── */
    int sockfd = socket(AF_INET6, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        fprintf(stderr, "Error: socket creation failed: %s\n", strerror(errno));
        munmap((void *)led_base, MAP_SIZE);
        close(memfd);
        return 1;
    }

    int opt = 1;
    setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

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

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = signal_handler;
    sa.sa_flags = 0; /* Do NOT set SA_RESTART so recvfrom() returns EINTR on SIGTERM */
    sigaction(SIGINT,  &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    printf("LED server v%s listening on UDP port %d\n", APP_VERSION_STR, port);
    printf("Initial LED pattern: 0x%02X\n", led_read(led_base));
    fflush(stdout);

    uint8_t  req[REQUEST_LEN];
    uint8_t  resp[RESPONSE_LEN];
    struct sockaddr_in6 client_addr;
    socklen_t client_len = sizeof(client_addr);

    while (running) {
        client_len = sizeof(client_addr);
        ssize_t nbytes = recvfrom(
            sockfd, req, sizeof(req), 0,
            (struct sockaddr *)&client_addr, &client_len
        );

        if (nbytes < 0) {
            if (errno == EINTR)
                break;
            fprintf(stderr, "recvfrom error: %s\n", strerror(errno));
            continue;
        }

        if (nbytes != REQUEST_LEN) {
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
            break;

        case CMD_GET_PATTERN:
            resp[0] = STATUS_OK;
            resp[1] = led_read(led_base);
            break;

        default:
            resp[0] = STATUS_ERR_UNKNOWN_CMD;
            resp[1] = led_read(led_base);
            break;
        }

        sendto(sockfd, resp, RESPONSE_LEN, 0,
               (struct sockaddr *)&client_addr, client_len);
    }

    led_write(led_base, 0x00);
    close(sockfd);
    munmap((void *)led_base, MAP_SIZE);
    close(memfd);
    return 0;
}

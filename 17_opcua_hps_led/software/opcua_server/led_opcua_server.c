/*
 * led_opcua_server.c — Industrial OPC UA Server (opc.tcp + Part 14 UADP PubSub)
 *                      for FPGA LED Control on the Terasic DE10-Nano.
 *
 * Phase 11 (17_opcua_hps_led):
 *   Exposes the Cyclone V FPGA LED PIO peripheral (at LWHPS2FPGA bridge
 *   0xFF200000) as a self-describing, browsable OPC UA Information Model on
 *   TCP port 4840 (opc.tcp://) and streams cyclic process telemetry via
 *   OPC UA Part 14 UADP Binary PubSub on UDP port 4841.
 *
 * Compare directly with Phase 7 (11_ethernet_hps_led):
 *   - led_server.c       (UDP port 5005): 2-byte positional binary protocol
 *   - led_server_pb.c    (UDP port 5006): Nanopb Protocol Buffers (.proto required)
 *   - led_opcua_server.c (TCP port 4840): Self-describing OPC UA Address Space
 *                                         (Browse, Read, Write, Call, Subscribe,
 *                                          and UDP UADP PubSub on port 4841)
 */

#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>
#include <errno.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>

#include "ua_micro.h"

/* ── Hardware Constants (Same FPGA Bitstream as Phases 3, 6, 7, 10) ──────── */
#define LWH2F_BASE       0xFF200000UL
#define MAP_SIZE         0x1000UL
#define PIO_DATA_OFFSET  0x00

static volatile sig_atomic_t g_running = 1;

static void signal_handler(int sig)
{
    (void)sig;
    g_running = 0;
}

/* ── Server Hardware & Telemetry State ───────────────────────────────────── */
typedef struct {
    volatile uint32_t *led_reg;       /* Points to /dev/mem mmap or sim_reg */
    uint32_t           sim_reg;       /* Backing store in --sim mode */
    bool               sim_mode;      /* True when running without /dev/mem */
    uint32_t           op_count;      /* Total hardware operations executed */
    time_t             start_time;    /* Server boot timestamp */
} HardwareContext;

static inline uint8_t hw_read_leds(HardwareContext *ctx)
{
    return (uint8_t)(*(ctx->led_reg) & 0xFFu);
}

static inline void hw_write_leds(HardwareContext *ctx, uint8_t val)
{
    *(ctx->led_reg) = (uint32_t)val;
    ctx->op_count++;
}

static void read_fpga_mgr_state(HardwareContext *ctx, char *buf, size_t cap)
{
    if (ctx->sim_mode) {
        strncpy(buf, "operating (simulated)", cap - 1);
        buf[cap - 1] = '\0';
        return;
    }
    FILE *fp = fopen("/sys/class/fpga_manager/fpga0/state", "r");
    if (!fp) {
        strncpy(buf, "unknown", cap - 1);
        buf[cap - 1] = '\0';
        return;
    }
    if (fgets(buf, (int)cap, fp)) {
        size_t len = strlen(buf);
        while (len > 0 && (buf[len - 1] == '\n' || buf[len - 1] == '\r')) {
            buf[--len] = '\0';
        }
    } else {
        strncpy(buf, "unknown", cap - 1);
        buf[cap - 1] = '\0';
    }
    fclose(fp);
}

/* ── OPC UA DataSource Read / Write Callbacks ────────────────────────────── */

static UA_StatusCode ds_read_led_pattern(void *user_ctx,
                                         const UA_NodeId *node_id,
                                         UA_Variant *out_val)
{
    (void)node_id;
    HardwareContext *ctx = (HardwareContext *)user_ctx;
    out_val->type     = UA_TYPE_BYTE;
    out_val->value.u8 = hw_read_leds(ctx);
    return UA_STATUSCODE_GOOD;
}

static UA_StatusCode ds_write_led_pattern(void *user_ctx,
                                          const UA_NodeId *node_id,
                                          const UA_Variant *in_val)
{
    (void)node_id;
    HardwareContext *ctx = (HardwareContext *)user_ctx;
    if (in_val->type != UA_TYPE_BYTE) {
        return UA_STATUSCODE_BADTYPEMISMATCH;
    }
    hw_write_leds(ctx, in_val->value.u8);
    printf("[OPC UA] Write %-22s <- 0x%02X\n",
           node_id->identifier, in_val->value.u8);
    fflush(stdout);
    return UA_STATUSCODE_GOOD;
}

static int parse_led_bit_index(const UA_NodeId *node_id)
{
    /* Matches "fpga.led0" .. "fpga.led7" */
    if (strncmp(node_id->identifier, "fpga.led", 8) == 0 &&
        node_id->identifier[8] >= '0' && node_id->identifier[8] <= '7' &&
        node_id->identifier[9] == '\0') {
        return node_id->identifier[8] - '0';
    }
    return -1;
}

static UA_StatusCode ds_read_led_bit(void *user_ctx,
                                     const UA_NodeId *node_id,
                                     UA_Variant *out_val)
{
    HardwareContext *ctx = (HardwareContext *)user_ctx;
    int bit = parse_led_bit_index(node_id);
    if (bit < 0) return UA_STATUSCODE_BADNODEIDUNKNOWN;

    out_val->type    = UA_TYPE_BOOLEAN;
    out_val->value.b = ((hw_read_leds(ctx) >> bit) & 0x01u) != 0;
    return UA_STATUSCODE_GOOD;
}

static UA_StatusCode ds_write_led_bit(void *user_ctx,
                                      const UA_NodeId *node_id,
                                      const UA_Variant *in_val)
{
    HardwareContext *ctx = (HardwareContext *)user_ctx;
    int bit = parse_led_bit_index(node_id);
    if (bit < 0) return UA_STATUSCODE_BADNODEIDUNKNOWN;
    if (in_val->type != UA_TYPE_BOOLEAN) return UA_STATUSCODE_BADTYPEMISMATCH;

    uint8_t cur = hw_read_leds(ctx);
    if (in_val->value.b) {
        cur |= (uint8_t)(1u << bit);
    } else {
        cur &= (uint8_t)~(1u << bit);
    }
    hw_write_leds(ctx, cur);
    printf("[OPC UA] Write %-22s <- %s (LED=0x%02X)\n",
           node_id->identifier, in_val->value.b ? "true" : "false", cur);
    fflush(stdout);
    return UA_STATUSCODE_GOOD;
}

static UA_StatusCode ds_read_bitstream_state(void *user_ctx,
                                             const UA_NodeId *node_id,
                                             UA_Variant *out_val)
{
    (void)node_id;
    HardwareContext *ctx = (HardwareContext *)user_ctx;
    out_val->type = UA_TYPE_STRING;
    read_fpga_mgr_state(ctx, out_val->value.str, sizeof(out_val->value.str));
    return UA_STATUSCODE_GOOD;
}

static UA_StatusCode ds_read_op_count(void *user_ctx,
                                      const UA_NodeId *node_id,
                                      UA_Variant *out_val)
{
    (void)node_id;
    HardwareContext *ctx = (HardwareContext *)user_ctx;
    out_val->type      = UA_TYPE_UINT32;
    out_val->value.u32 = ctx->op_count;
    return UA_STATUSCODE_GOOD;
}

static UA_StatusCode ds_read_uptime(void *user_ctx,
                                    const UA_NodeId *node_id,
                                    UA_Variant *out_val)
{
    (void)node_id;
    HardwareContext *ctx = (HardwareContext *)user_ctx;
    time_t now = time(NULL);
    out_val->type      = UA_TYPE_UINT32;
    out_val->value.u32 = (now >= ctx->start_time) ? (uint32_t)(now - ctx->start_time) : 0u;
    return UA_STATUSCODE_GOOD;
}

/* ── OPC UA Method Callbacks (Call Service 712) ─────────────────────────── */

static UA_StatusCode method_set_pattern(void *user_ctx,
                                        const UA_NodeId *method_id,
                                        const UA_Variant *in_arg,
                                        UA_Variant *out_arg)
{
    (void)method_id;
    HardwareContext *ctx = (HardwareContext *)user_ctx;
    uint32_t pattern = 0;

    if (in_arg->type == UA_TYPE_BYTE) {
        pattern = in_arg->value.u8;
    } else if (in_arg->type == UA_TYPE_UINT32) {
        if (in_arg->value.u32 > 0xFFu) {
            return UA_STATUSCODE_BADOUTOFRANGE;
        }
        pattern = in_arg->value.u32;
    } else {
        return UA_STATUSCODE_BADTYPEMISMATCH;
    }

    hw_write_leds(ctx, (uint8_t)pattern);
    out_arg->type     = UA_TYPE_BYTE;
    out_arg->value.u8 = hw_read_leds(ctx);
    printf("[OPC UA] Call  fpga.set_pattern(0x%02X) -> LED=0x%02X\n",
           (unsigned)pattern, out_arg->value.u8);
    fflush(stdout);
    return UA_STATUSCODE_GOOD;
}

static UA_StatusCode method_invert_pattern(void *user_ctx,
                                           const UA_NodeId *method_id,
                                           const UA_Variant *in_arg,
                                           UA_Variant *out_arg)
{
    (void)method_id;
    (void)in_arg;
    HardwareContext *ctx = (HardwareContext *)user_ctx;
    uint8_t inverted = (uint8_t)(~hw_read_leds(ctx) & 0xFFu);
    hw_write_leds(ctx, inverted);
    out_arg->type     = UA_TYPE_BYTE;
    out_arg->value.u8 = hw_read_leds(ctx);
    printf("[OPC UA] Call  fpga.invert_pattern()    -> LED=0x%02X\n",
           out_arg->value.u8);
    fflush(stdout);
    return UA_STATUSCODE_GOOD;
}

/* ── Build the DE10-Nano Information Model (Address Space) ───────────────── */

static void build_de10nano_address_space(UA_Server *server, HardwareContext *ctx)
{
    /* 1. Root Device Object: Objects/DE10Nano */
    UA_Server_addObjectNode(
        server,
        UA_NODEID_STRING(1, "de10nano"),
        "Objects",
        "DE10Nano",
        "Terasic DE10-Nano (Cyclone V SoC)",
        "Industrial Edge Controller — ARM Cortex-A9 HPS + Cyclone V FPGA"
    );

    /* 2. FPGA Subsystem Object: Objects/DE10Nano/FPGA */
    UA_Server_addObjectNode(
        server,
        UA_NODEID_STRING(1, "fpga"),
        "de10nano",
        "FPGA",
        "FPGA Fabric Peripherals (LWH2F @ 0xFF200000)",
        "Avalon-MM Slave peripherals mapped via Lightweight HPS-to-FPGA bridge"
    );

    /* 3. Variable: FPGA.LedPattern (Byte, Read/Write) */
    UA_DataSource led_pattern_ds = {
        .read  = ds_read_led_pattern,
        .write = ds_write_led_pattern
    };
    UA_Server_addDataSourceVariableNode(
        server,
        UA_NODEID_STRING(1, "fpga.led_pattern"),
        "fpga",
        "LedPattern",
        "8-Bit LED Pattern Register",
        "Bitmask (0x00..0xFF) driving physical LED[7:0] on the DE10-Nano",
        UA_TYPE_BYTE,
        UA_ACCESSLEVELMASK_READWRITE,
        led_pattern_ds,
        ctx
    );

    /* 4. Variables: Individual LED Bits FPGA.Led0 .. FPGA.Led7 (Boolean, R/W) */
    UA_DataSource led_bit_ds = {
        .read  = ds_read_led_bit,
        .write = ds_write_led_bit
    };
    for (int i = 0; i < 8; i++) {
        char id_str[32], bname[16], dname[32], desc[64];
        snprintf(id_str, sizeof(id_str), "fpga.led%d", i);
        snprintf(bname,  sizeof(bname),  "Led%d", i);
        snprintf(dname,  sizeof(dname),  "LED[%d] State", i);
        snprintf(desc,   sizeof(desc),   "Individual boolean state of physical FPGA LED[%d]", i);

        UA_Server_addDataSourceVariableNode(
            server,
            UA_NODEID_STRING(1, id_str),
            "fpga",
            bname,
            dname,
            desc,
            UA_TYPE_BOOLEAN,
            UA_ACCESSLEVELMASK_READWRITE,
            led_bit_ds,
            ctx
        );
    }

    /* 5. Variable: FPGA.BitstreamState (String, Read-Only) */
    UA_DataSource bs_state_ds = {
        .read  = ds_read_bitstream_state,
        .write = NULL
    };
    UA_Server_addDataSourceVariableNode(
        server,
        UA_NODEID_STRING(1, "fpga.bitstream_state"),
        "fpga",
        "BitstreamState",
        "FPGA Manager State",
        "Linux FPGA Manager status from /sys/class/fpga_manager/fpga0/state",
        UA_TYPE_STRING,
        UA_ACCESSLEVELMASK_READ,
        bs_state_ds,
        ctx
    );

    /* 6. Method: FPGA.SetPattern(pattern) -> new_pattern */
    UA_Server_addMethodNode(
        server,
        UA_NODEID_STRING(1, "fpga.set_pattern"),
        "fpga",
        "SetPattern",
        "Set 8-Bit LED Pattern",
        "Validates and writes an 8-bit pattern (0x00..0xFF) to the FPGA LED PIO",
        method_set_pattern,
        ctx
    );

    /* 7. Method: FPGA.InvertPattern() -> new_pattern */
    UA_Server_addMethodNode(
        server,
        UA_NODEID_STRING(1, "fpga.invert_pattern"),
        "fpga",
        "InvertPattern",
        "Invert 8-Bit LED Pattern",
        "Bitwise-inverts (~pattern & 0xFF) the current FPGA LED PIO register",
        method_invert_pattern,
        ctx
    );

    /* 8. Diagnostics Object: Objects/DE10Nano/Diagnostics */
    UA_Server_addObjectNode(
        server,
        UA_NODEID_STRING(1, "diag"),
        "de10nano",
        "Diagnostics",
        "Server & Bridge Diagnostics",
        "Operational counters and uptime telemetry"
    );

    UA_DataSource op_count_ds = { .read = ds_read_op_count, .write = NULL };
    UA_Server_addDataSourceVariableNode(
        server,
        UA_NODEID_STRING(1, "diag.op_count"),
        "diag",
        "OperationCount",
        "MMIO Write/Call Counter",
        "Total number of hardware LED register writes since server startup",
        UA_TYPE_UINT32,
        UA_ACCESSLEVELMASK_READ,
        op_count_ds,
        ctx
    );

    UA_DataSource uptime_ds = { .read = ds_read_uptime, .write = NULL };
    UA_Server_addDataSourceVariableNode(
        server,
        UA_NODEID_STRING(1, "diag.uptime_sec"),
        "diag",
        "UptimeSeconds",
        "Server Uptime (s)",
        "Elapsed seconds since led_opcua_server started",
        UA_TYPE_UINT32,
        UA_ACCESSLEVELMASK_READ,
        uptime_ds,
        ctx
    );
}

/* ── Per-Client TCP Session & Subscription Tracking ──────────────────────── */
typedef struct {
    int                 fd;
    bool                subscribed_leds;
    uint32_t            notify_seq;
    struct sockaddr_in6 peer_addr;
} ClientSlot;

static bool read_exact(int fd, uint8_t *buf, size_t n)
{
    size_t got = 0;
    while (got < n) {
        ssize_t r = recv(fd, buf + got, n - got, 0);
        if (r <= 0) return false;
        got += (size_t)r;
    }
    return true;
}

static void push_led_notification(ClientSlot *slot, uint8_t led_val)
{
    uint8_t tx[256];
    UA_NodeId nid = UA_NODEID_STRING(1, "fpga.led_pattern");
    UA_Variant val;
    memset(&val, 0, sizeof(val));
    val.type     = UA_TYPE_BYTE;
    val.value.u8 = led_val;

    slot->notify_seq++;
    size_t len = UA_Server_encodeDataChangeNotification(
        1u, 1u, slot->notify_seq, slot->notify_seq, &nid, &val, tx, sizeof(tx)
    );
    if (len > 0) {
        (void)send(slot->fd, tx, len, MSG_NOSIGNAL);
    }
}

static void usage(const char *prog)
{
    printf("Usage: %s [OPTIONS]\n\n", prog);
    printf("Industrial OPC UA Server (opc.tcp + Part 14 UADP PubSub) for DE10-Nano.\n\n");
    printf("Options:\n");
    printf("  -p, --port PORT          OPC UA TCP port (default: %d)\n", UA_DEFAULT_TCP_PORT);
    printf("  -u, --pubsub-port PORT   UDP destination port for UADP PubSub (default: %d, 0=off)\n",
           UA_DEFAULT_PUBSUB_PORT);
    printf("  -i, --interval-ms MS     UADP PubSub cyclic interval in ms (default: 1000)\n");
    printf("  -s, --sim                Simulate FPGA MMIO in RAM (for host testing without /dev/mem)\n");
    printf("  -h, --help               Show this help message\n");
}

int main(int argc, char *argv[])
{
    int tcp_port        = UA_DEFAULT_TCP_PORT;
    int pubsub_port     = UA_DEFAULT_PUBSUB_PORT;
    int pubsub_ms       = 1000;
    bool sim_mode       = false;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            usage(argv[0]);
            return 0;
        } else if ((strcmp(argv[i], "-p") == 0 || strcmp(argv[i], "--port") == 0) && i + 1 < argc) {
            tcp_port = atoi(argv[++i]);
        } else if ((strcmp(argv[i], "-u") == 0 || strcmp(argv[i], "--pubsub-port") == 0) && i + 1 < argc) {
            pubsub_port = atoi(argv[++i]);
        } else if ((strcmp(argv[i], "-i") == 0 || strcmp(argv[i], "--interval-ms") == 0) && i + 1 < argc) {
            pubsub_ms = atoi(argv[++i]);
            if (pubsub_ms < 50) pubsub_ms = 50;
        } else if (strcmp(argv[i], "-s") == 0 || strcmp(argv[i], "--sim") == 0) {
            sim_mode = true;
        } else {
            fprintf(stderr, "Error: unknown argument '%s'\n", argv[i]);
            usage(argv[0]);
            return 1;
        }
    }

    /* ── Initialize Hardware or Simulated MMIO Context ──────────────────── */
    HardwareContext hw_ctx;
    memset(&hw_ctx, 0, sizeof(hw_ctx));
    hw_ctx.sim_mode   = sim_mode;
    hw_ctx.sim_reg    = 0x0Fu; /* Default startup pattern in sim mode */
    hw_ctx.start_time = time(NULL);

    int memfd = -1;
    void *mapped_base = MAP_FAILED;

    if (sim_mode) {
        hw_ctx.led_reg = &hw_ctx.sim_reg;
    } else {
        memfd = open("/dev/mem", O_RDWR | O_SYNC);
        if (memfd < 0) {
            fprintf(stderr, "Error: cannot open /dev/mem (%s). Use --sim on host PC.\n",
                    strerror(errno));
            return 1;
        }
        mapped_base = mmap(NULL, MAP_SIZE, PROT_READ | PROT_WRITE,
                           MAP_SHARED, memfd, LWH2F_BASE);
        if (mapped_base == MAP_FAILED) {
            fprintf(stderr, "Error: mmap(0x%08lX) failed: %s\n",
                    (unsigned long)LWH2F_BASE, strerror(errno));
            close(memfd);
            return 1;
        }
        hw_ctx.led_reg = (volatile uint32_t *)((uint8_t *)mapped_base + PIO_DATA_OFFSET);
    }

    /* ── Build OPC UA Server & Address Space ────────────────────────────── */
    char endpoint[UA_MAX_STRING_LEN];
    snprintf(endpoint, sizeof(endpoint), "opc.tcp://0.0.0.0:%d", tcp_port);

    UA_Server server;
    UA_Server_init(&server, endpoint);
    build_de10nano_address_space(&server, &hw_ctx);

    /* ── Create Dual-Stack TCP Listening Socket (opc.tcp) ───────────────── */
    int listen_fd = socket(AF_INET6, SOCK_STREAM, 0);
    if (listen_fd < 0) {
        perror("socket(AF_INET6)");
        return 1;
    }
    int opt = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    int v6only = 0;
    setsockopt(listen_fd, IPPROTO_IPV6, IPV6_V6ONLY, &v6only, sizeof(v6only));

    struct sockaddr_in6 srv_addr;
    memset(&srv_addr, 0, sizeof(srv_addr));
    srv_addr.sin6_family = AF_INET6;
    srv_addr.sin6_port   = htons((uint16_t)tcp_port);
    srv_addr.sin6_addr   = in6addr_any;

    if (bind(listen_fd, (struct sockaddr *)&srv_addr, sizeof(srv_addr)) < 0) {
        fprintf(stderr, "Error: bind port %d failed: %s\n", tcp_port, strerror(errno));
        close(listen_fd);
        return 1;
    }
    if (listen(listen_fd, 8) < 0) {
        perror("listen");
        close(listen_fd);
        return 1;
    }

    /* ── Optional Dual-Stack UDP Socket for OPC UA Part 14 UADP PubSub ──── */
    int pubsub_fd = -1;
    struct sockaddr_in6 pubsub_loopback;
    struct sockaddr_in6 last_peer_addr;
    bool has_last_peer = false;
    uint16_t pubsub_seq = 0;
    memset(&last_peer_addr, 0, sizeof(last_peer_addr));
    if (pubsub_port > 0) {
        pubsub_fd = socket(AF_INET6, SOCK_DGRAM, 0);
        if (pubsub_fd >= 0) {
            int v6off = 0;
            setsockopt(pubsub_fd, IPPROTO_IPV6, IPV6_V6ONLY, &v6off, sizeof(v6off));
            memset(&pubsub_loopback, 0, sizeof(pubsub_loopback));
            pubsub_loopback.sin6_family = AF_INET6;
            pubsub_loopback.sin6_port   = htons((uint16_t)pubsub_port);
            /* IPv4-mapped loopback ::ffff:127.0.0.1 so both AF_INET and AF_INET6 local listeners work */
            inet_pton(AF_INET6, "::ffff:127.0.0.1", &pubsub_loopback.sin6_addr);
        }
    }

    signal(SIGINT,  signal_handler);
    signal(SIGTERM, signal_handler);

    printf("===================================================================\n");
    printf("  DE10-Nano Industrial OPC UA Server (Phase 11 — 17_opcua_hps_led)\n");
    printf("  Endpoint:    opc.tcp://[::]:%d (%zu nodes in Address Space)\n",
           tcp_port, server.node_count);
    printf("  UADP PubSub: UDP port %d (interval: %d ms)\n", pubsub_port, pubsub_ms);
    printf("  Hardware:    %s (initial LED=0x%02X)\n",
           sim_mode ? "Simulated RAM register (--sim)" : "/dev/mem @ 0xFF200000",
           hw_read_leds(&hw_ctx));
    printf("===================================================================\n");
    fflush(stdout);

    ClientSlot clients[UA_MAX_CLIENTS];
    for (int i = 0; i < UA_MAX_CLIENTS; i++) {
        clients[i].fd = -1;
        clients[i].subscribed_leds = false;
        clients[i].notify_seq = 0;
    }

    uint8_t rx_buf[UA_MAX_PACKET_SIZE];
    uint8_t tx_buf[UA_MAX_PACKET_SIZE];
    uint8_t last_published_led = hw_read_leds(&hw_ctx);

    while (g_running) {
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(listen_fd, &rfds);
        int max_fd = listen_fd;

        for (int i = 0; i < UA_MAX_CLIENTS; i++) {
            if (clients[i].fd >= 0) {
                FD_SET(clients[i].fd, &rfds);
                if (clients[i].fd > max_fd) max_fd = clients[i].fd;
            }
        }

        struct timeval tv;
        tv.tv_sec  = pubsub_ms / 1000;
        tv.tv_usec = (pubsub_ms % 1000) * 1000;

        int ready = select(max_fd + 1, &rfds, NULL, NULL, &tv);
        if (ready < 0) {
            if (errno == EINTR) break;
            perror("select");
            continue;
        }

        /* 1. Accept incoming OPC UA TCP client connections */
        if (ready > 0 && FD_ISSET(listen_fd, &rfds)) {
            struct sockaddr_in6 caddr;
            socklen_t clen = sizeof(caddr);
            int cfd = accept(listen_fd, (struct sockaddr *)&caddr, &clen);
            if (cfd >= 0) {
                int nodelay = 1;
                setsockopt(cfd, IPPROTO_TCP, TCP_NODELAY, &nodelay, sizeof(nodelay));
                bool placed = false;
                for (int i = 0; i < UA_MAX_CLIENTS; i++) {
                    if (clients[i].fd < 0) {
                        clients[i].fd = cfd;
                        clients[i].subscribed_leds = false;
                        clients[i].notify_seq = 0;
                        clients[i].peer_addr = caddr;
                        last_peer_addr = caddr;
                        last_peer_addr.sin6_port = htons((uint16_t)pubsub_port);
                        has_last_peer = true;
                        placed = true;
                        break;
                    }
                }
                if (!placed) {
                    close(cfd);
                }
            }
        }

        /* 2. Process incoming chunks from connected clients */
        for (int i = 0; i < UA_MAX_CLIENTS; i++) {
            int cfd = clients[i].fd;
            if (cfd < 0 || !FD_ISSET(cfd, &rfds)) continue;

            /* Read 8-byte OPC UA TCP Chunk Header: [Type(3)][Chunk(1)][Size(4)] */
            if (!read_exact(cfd, rx_buf, 8)) {
                close(cfd);
                clients[i].fd = -1;
                continue;
            }

            uint32_t msg_size = (uint32_t)rx_buf[4]
                              | ((uint32_t)rx_buf[5] << 8)
                              | ((uint32_t)rx_buf[6] << 16)
                              | ((uint32_t)rx_buf[7] << 24);
            if (msg_size < 8 || msg_size > UA_MAX_PACKET_SIZE) {
                close(cfd);
                clients[i].fd = -1;
                continue;
            }

            if (msg_size > 8 && !read_exact(cfd, rx_buf + 8, msg_size - 8)) {
                close(cfd);
                clients[i].fd = -1;
                continue;
            }

            /* Check if this chunk is a CreateSubscriptionRequest (787) */
            bool is_sub_req = false;
            if (msg_size >= 28 && memcmp(rx_buf, "MSGF", 4) == 0) {
                uint16_t svc_id = (uint16_t)rx_buf[26] | ((uint16_t)rx_buf[27] << 8);
                if (svc_id == UA_SVC_CREATESUB_REQ) {
                    is_sub_req = true;
                }
            }

            bool should_close = false;
            int resp_len = UA_Server_processChunk(
                &server, rx_buf, msg_size, tx_buf, sizeof(tx_buf), &should_close
            );
            if (resp_len > 0) {
                (void)send(cfd, tx_buf, (size_t)resp_len, MSG_NOSIGNAL);
                /* If CreateSubscription succeeded, mark client & send initial value */
                if (is_sub_req && resp_len >= 32 && tx_buf[28] == 0x00) {
                    clients[i].subscribed_leds = true;
                    push_led_notification(&clients[i], hw_read_leds(&hw_ctx));
                }
            }
            if (should_close || resp_len < 0) {
                close(cfd);
                clients[i].fd = -1;
            }
        }

        /* 3. Report-by-Exception: Push DataChangeNotification if LED changed */
        uint8_t current_led = hw_read_leds(&hw_ctx);
        if (current_led != last_published_led) {
            last_published_led = current_led;
            for (int i = 0; i < UA_MAX_CLIENTS; i++) {
                if (clients[i].fd >= 0 && clients[i].subscribed_leds) {
                    push_led_notification(&clients[i], current_led);
                }
            }
        }

        /* 4. Emit OPC UA Part 14 UADP PubSub cyclic frame on timeout or change */
        if (pubsub_fd >= 0 && ready == 0) {
            uint8_t uadp_buf[64];
            time_t now = time(NULL);
            uint32_t up = (now >= hw_ctx.start_time) ? (uint32_t)(now - hw_ctx.start_time) : 0u;
            pubsub_seq++;
            size_t ulen = UA_PubSub_encodeUadpTelemetry(
                0x000Au, 0x0001u, pubsub_seq,
                current_led, hw_ctx.op_count, up,
                uadp_buf, sizeof(uadp_buf)
            );
            if (ulen > 0) {
                (void)sendto(pubsub_fd, uadp_buf, ulen, 0,
                             (struct sockaddr *)&pubsub_loopback, sizeof(pubsub_loopback));
                if (has_last_peer &&
                    !IN6_IS_ADDR_LOOPBACK(&last_peer_addr.sin6_addr) &&
                    memcmp(&last_peer_addr.sin6_addr, &pubsub_loopback.sin6_addr, 16) != 0) {
                    (void)sendto(pubsub_fd, uadp_buf, ulen, 0,
                                 (struct sockaddr *)&last_peer_addr, sizeof(last_peer_addr));
                }
            }
        }
    }

    printf("\nShutting down OPC UA server.\n");
    for (int i = 0; i < UA_MAX_CLIENTS; i++) {
        if (clients[i].fd >= 0) close(clients[i].fd);
    }
    if (pubsub_fd >= 0) close(pubsub_fd);
    close(listen_fd);

    if (!sim_mode) {
        hw_write_leds(&hw_ctx, 0x00u);
        if (mapped_base != MAP_FAILED) munmap(mapped_base, MAP_SIZE);
        if (memfd >= 0) close(memfd);
    }
    return 0;
}

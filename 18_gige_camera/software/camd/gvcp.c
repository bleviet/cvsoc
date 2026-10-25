/*
 * gvcp.c — GVCP command handling (see gvcp.h).
 *
 * Command header: key 0x42, flags, command, payload length, request ID.
 * Ack header:     status, ack command (= command + 1), payload length, ack ID.
 */
#define _GNU_SOURCE
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <linux/if_packet.h>
#include <net/if.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "gvcp.h"
#include "util.h"

int gvcp_open(uint16_t port)
{
    struct sockaddr_in addr = {
        .sin_family = AF_INET, .sin_port = htons(port),
        .sin_addr.s_addr = htonl(INADDR_ANY),
    };
    int one = 1;
    int fd = socket(AF_INET, SOCK_DGRAM, 0);

    if (fd < 0) {
        perror("camd: socket");
        return -1;
    }
    setsockopt(fd, SOL_SOCKET, SO_BROADCAST, &one, sizeof(one));
    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("camd: bind GVCP port");
        close(fd);
        return -1;
    }
    return fd;
}

/*
 * The address, mask and MAC of the interface this host uses to reach `peer`,
 * reported in the discovery ack.
 */
static void identify_interface(const struct sockaddr_in *peer, struct in_addr *ip,
                               struct in_addr *mask, uint8_t mac[6])
{
    struct sockaddr_in local;
    socklen_t len = sizeof(local);
    struct ifaddrs *ifs, *i;
    char ifname[IFNAMSIZ] = "";
    int fd = socket(AF_INET, SOCK_DGRAM, 0);

    ip->s_addr = 0;
    mask->s_addr = 0;
    memset(mac, 0, 6);
    if (fd < 0)
        return;
    if (connect(fd, (const struct sockaddr *)peer, sizeof(*peer)) == 0 &&
        getsockname(fd, (struct sockaddr *)&local, &len) == 0)
        *ip = local.sin_addr;
    close(fd);

    if (getifaddrs(&ifs) < 0)
        return;
    for (i = ifs; i; i = i->ifa_next) {
        if (i->ifa_addr && i->ifa_addr->sa_family == AF_INET &&
            ((struct sockaddr_in *)i->ifa_addr)->sin_addr.s_addr == ip->s_addr) {
            snprintf(ifname, sizeof(ifname), "%s", i->ifa_name);
            if (i->ifa_netmask)
                *mask = ((struct sockaddr_in *)i->ifa_netmask)->sin_addr;
        }
    }
    for (i = ifs; i && ifname[0]; i = i->ifa_next) {
        if (i->ifa_addr && i->ifa_addr->sa_family == AF_PACKET &&
            strcmp(i->ifa_name, ifname) == 0) {
            const struct sockaddr_ll *ll = (const struct sockaddr_ll *)i->ifa_addr;

            if (ll->sll_halen == 6)
                memcpy(mac, ll->sll_addr, 6);
        }
    }
    freeifaddrs(ifs);
}

void gvcp_handle(struct camera *c, int fd, const uint8_t *pkt, size_t len,
                 const struct sockaddr_in *peer, int verbose)
{
    uint8_t ack[GVCP_MAX_PACKET];
    uint8_t *out = ack + GVCP_HDR_LEN;
    const uint8_t *in = pkt + GVCP_HDR_LEN;
    uint16_t cmd, plen, req_id, status = GEV_STATUS_SUCCESS;
    size_t alen = 0;
    uint8_t flags;

    if (len < GVCP_HDR_LEN || pkt[0] != GVCP_KEY)
        return;
    flags = pkt[1];
    cmd = get_be16(&pkt[2]);
    plen = get_be16(&pkt[4]);
    req_id = get_be16(&pkt[6]);
    if ((size_t)plen + GVCP_HDR_LEN > len)
        return;

    cam_seen(c, peer, now_ns());

    switch (cmd) {
    case GVCP_DISCOVERY_CMD: {
        struct in_addr ip, mask;
        uint8_t mac[6];

        identify_interface(peer, &ip, &mask, mac);
        cam_set_network(c, mac, ip, mask);
        status = cam_read(c, 0, out, BS_DISCOVERY_DATA_LEN, NULL);
        alen = BS_DISCOVERY_DATA_LEN;
        break;
    }

    case GVCP_READREG_CMD:
        if (plen % 4 || plen / 4 > GVCP_MAX_REGS) {
            status = GEV_STATUS_INVALID_PARAMETER;
            break;
        }
        for (size_t i = 0; i < plen / 4; i++) {
            uint32_t addr = get_be32(&in[i * 4]);

            status = (addr & 3) ? GEV_STATUS_BAD_ALIGNMENT
                                : cam_read(c, addr, &out[i * 4], 4, peer);
            if (status != GEV_STATUS_SUCCESS)
                break;
            alen += 4;
        }
        break;

    case GVCP_WRITEREG_CMD: {
        uint16_t written = 0;

        if (plen % 8 || plen / 8 > GVCP_MAX_REGS) {
            status = GEV_STATUS_INVALID_PARAMETER;
        } else {
            for (size_t i = 0; i < plen / 8; i++) {
                status = cam_write_reg(c, get_be32(&in[i * 8]), get_be32(&in[i * 8 + 4]),
                                       peer);
                if (status != GEV_STATUS_SUCCESS)
                    break;
                written++;
            }
        }
        put_be16(&out[0], 0);
        put_be16(&out[2], written);
        alen = 4;
        break;
    }

    case GVCP_READMEM_CMD: {
        uint32_t addr;
        uint16_t count;

        if (plen < 8) {
            status = GEV_STATUS_INVALID_PARAMETER;
            break;
        }
        addr = get_be32(&in[0]);
        count = get_be16(&in[6]);
        put_be32(&out[0], addr);
        alen = 4;
        if (count % 4 || count > GVCP_MAX_READMEM)
            status = GEV_STATUS_INVALID_PARAMETER;
        else if (addr & 3)
            status = GEV_STATUS_BAD_ALIGNMENT;
        else if ((status = cam_read(c, addr, &out[4], count, peer)) == GEV_STATUS_SUCCESS)
            alen += count;
        break;
    }

    case GVCP_WRITEMEM_CMD:
        if (plen < 4) {
            status = GEV_STATUS_INVALID_PARAMETER;
        } else {
            status = cam_write_mem(c, get_be32(&in[0]), &in[4], plen - 4u, peer);
        }
        put_be16(&out[0], 0);
        put_be16(&out[2], status == GEV_STATUS_SUCCESS ? (uint16_t)(plen - 4u) : 0);
        alen = 4;
        break;

    case GVCP_PACKETRESEND_CMD:
        return;   /* no ack defined; resend is not supported in M1 */

    default:
        status = GEV_STATUS_NOT_IMPLEMENTED;
        break;
    }

    if (verbose)
        fprintf(stderr, "camd: %s:%u cmd 0x%04x len %u -> status 0x%04x\n",
                inet_ntoa(peer->sin_addr), ntohs(peer->sin_port), cmd, plen, status);

    if (!(flags & GVCP_FLAG_ACK_REQUIRED))
        return;
    put_be16(&ack[0], status);
    put_be16(&ack[2], (uint16_t)(cmd + 1));
    put_be16(&ack[4], (uint16_t)alen);
    put_be16(&ack[6], req_id);
    sendto(fd, ack, GVCP_HDR_LEN + alen, 0, (const struct sockaddr *)peer, sizeof(*peer));
}

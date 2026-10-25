/*
 * eth_pause.c — show or set Ethernet flow control (IEEE 802.3x pause frames)
 * on images without ethtool.
 *
 * With pause enabled, a slow receiver throttles gvsp_blast through pause
 * frames, and the spike then measures the receiver instead of the board.
 * Turning pause off makes the board send at its own limit, and any
 * shortfall shows up as loss at the sink.
 *
 * Usage:
 *   eth_pause eth0          — show the current setting
 *   eth_pause eth0 off      — force rx/tx pause off (link renegotiates)
 *   eth_pause eth0 on       — restore autonegotiated rx/tx pause
 */
#define _GNU_SOURCE
#include <linux/ethtool.h>
#include <linux/sockios.h>
#include <net/if.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

static int pause_ioctl(int fd, const char *ifname, struct ethtool_pauseparam *pp)
{
    struct ifreq ifr;

    memset(&ifr, 0, sizeof(ifr));
    strncpy(ifr.ifr_name, ifname, IFNAMSIZ - 1);
    ifr.ifr_data = (char *)pp;
    return ioctl(fd, SIOCETHTOOL, &ifr);
}

int main(int argc, char **argv)
{
    struct ethtool_pauseparam pp = { .cmd = ETHTOOL_GPAUSEPARAM };
    int fd;

    if (argc < 2 || argc > 3 ||
        (argc == 3 && strcmp(argv[2], "on") != 0 && strcmp(argv[2], "off") != 0)) {
        fprintf(stderr, "Usage: %s IFACE [on|off]\n", argv[0]);
        return 2;
    }
    fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        perror("socket");
        return 1;
    }

    if (argc == 3) {
        int on = strcmp(argv[2], "on") == 0;

        pp = (struct ethtool_pauseparam){
            .cmd = ETHTOOL_SPAUSEPARAM, .autoneg = on,
            .rx_pause = on, .tx_pause = on,
        };
        if (pause_ioctl(fd, argv[1], &pp) < 0) {
            perror("ETHTOOL_SPAUSEPARAM");
            return 1;
        }
        pp = (struct ethtool_pauseparam){ .cmd = ETHTOOL_GPAUSEPARAM };
    }

    if (pause_ioctl(fd, argv[1], &pp) < 0) {
        perror("ETHTOOL_GPAUSEPARAM");
        return 1;
    }
    printf("%s: pause autoneg %s, rx %s, tx %s\n", argv[1],
           pp.autoneg ? "on" : "off", pp.rx_pause ? "on" : "off",
           pp.tx_pause ? "on" : "off");
    close(fd);
    return 0;
}

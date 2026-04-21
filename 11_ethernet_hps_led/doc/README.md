# Phase 7 — Ethernet LED Control (Track A: HPS Ethernet)

## Overview

This project extends the Phase 6 Embedded Linux setup with a UDP server that
accepts LED pattern commands from a PC over the network. It demonstrates the
complete hardware+software stack: PC → Ethernet → HPS Linux → FPGA LED PIO.

```
PC (Python client)
    │  UDP port 5005
    ▼
DE10-Nano HPS (ARM Cortex-A9, Linux)
    │  led_server: /usr/bin/led_server
    │  /dev/mem mmap @ 0xFF200000
    ▼
FPGA LED PIO (Avalon MM Slave)
    │  8-bit LED register
    ▼
DE10-Nano LEDs [7:0]
```

---

## Wire Protocol

All communication uses UDP. The protocol is intentionally minimal — one request
/ one response, no connection state.

### Request (2 bytes sent from PC to board)

| Byte | Field   | Values |
|------|---------|--------|
| 0    | CMD     | `0x01` = SET_PATTERN, `0x02` = GET_PATTERN |
| 1    | PATTERN | 8-bit LED bitmask (ignored for GET) |

### Response (2 bytes sent from board to PC)

| Byte | Field          | Values |
|------|----------------|--------|
| 0    | STATUS         | `0x00` = OK, `0x01` = ERR_UNKNOWN_CMD, `0x02` = ERR_INVALID_LEN |
| 1    | CURRENT_PATTERN | Current LED register value |

### Port

Default: **UDP 5005**. Configurable with `led_server --port <N>`.

> **§7.5 Note:** This raw byte protocol is the Phase 7 baseline.  
> §7.5 of the roadmap replaces it with a protobuf (`nanopb`) encoding
> while keeping the UDP transport and LED logic unchanged.

---

## Prerequisites

- Phase 6 (`10_linux_led`) must be built. The FPGA bitstream (`de10_nano.rbf`)
  and Phase 6 software sources are reused.
- The same hardware setup as Phase 6 (DE10-Nano SD card, UART serial console).

---

## How to Build

### Option A: Full Buildroot (produces a complete SD card image)

```sh
# Download Buildroot (first time only)
cd 11_ethernet_hps_led
make buildroot-download

# Configure and build
make all

# Write to SD card
sudo dd if=buildroot-2024.11.1/output/images/sdcard.img \
        of=/dev/sdX bs=4M status=progress
```

### Option B: Standalone cross-compile (for quick iterations)

Cross-compile `led_server` and copy it to an already-running Phase 6 board:

```sh
make server-cross
scp software/led_server/led_server root@<board-ip>:/usr/bin/
```

Then on the board:

```sh
led_server &
```

---

## Running the Server

The Buildroot image installs an init script (`/etc/init.d/S40led_server`) that
starts the server automatically at boot. To control it manually:

```sh
# On the board (over serial or SSH)
led_server --port 5005       # foreground
led_server &                 # background
/etc/init.d/S40led_server start
```

Server log is written to `/var/log/led_server.log`.

---

## Using the PC Client

```sh
# Set LEDs to pattern 0xA5 (binary: 10100101)
python3 client/send_led_pattern.py --host 192.168.1.100 --pattern 0xA5

# Read current LED pattern
python3 client/send_led_pattern.py --host 192.168.1.100 --get

# Run a named animation (loops until Ctrl+C)
python3 client/send_led_pattern.py --host 192.168.1.100 --animate chase
python3 client/send_led_pattern.py --host 192.168.1.100 --animate breathe
python3 client/send_led_pattern.py --host 192.168.1.100 --animate all

# Use a different speed or port
python3 client/send_led_pattern.py --host 192.168.1.100 \
    --animate stripes --speed 200 --port 5005
```

Available animations: `chase`, `breathe`, `blink`, `stripes`, `all`.

---

## Network Setup

The Buildroot image enables DHCP on `eth0` at boot. To find the board IP:
- Check your router's DHCP lease table, or
- Connect a serial cable (`115200 8N1`) and run `ip addr show eth0`.

For a static IP, edit `/etc/network/interfaces` on the board or set
`BR2_SYSTEM_DHCP=""` and add a static configuration in `post-build.sh`.

---

## Running Unit Tests (no board required)

The Python protocol encoding/decoding is tested independently:

```sh
make test
# or directly:
cd client && python3 test_protocol.py
```

---

## Directory Layout

```
11_ethernet_hps_led/
├── Makefile                          Project orchestration
├── doc/
│   └── README.md                     This file
├── software/
│   └── led_server/
│       ├── led_server.c              UDP server (Phase 7 new)
│       └── Makefile
├── client/
│   ├── send_led_pattern.py           PC-side Python client
│   └── test_protocol.py             Protocol unit tests
└── br2-external/
    ├── board/de10_nano/              Board config (same hardware as Phase 6)
    │   ├── S30fpga_load              Init script: program FPGA at boot
    │   ├── S40led_server             Init script: start UDP server at boot
    │   ├── post-build.sh
    │   ├── post-image.sh
    │   ├── extlinux.conf
    │   ├── genimage.cfg
    │   ├── linux-uio.fragment
    │   └── uboot.fragment
    ├── configs/
    │   └── de10_nano_defconfig       Buildroot defconfig (extends Phase 6)
    └── package/
        ├── fpga-led/                 Reused from Phase 6
        ├── fpga-mgr-load/            Reused from Phase 6
        └── led-server/               New Phase 7 package
```

---

## Troubleshooting

| Symptom | Cause | Fix |
|---------|-------|-----|
| `make rbf` fails | Phase 6 RBF not built | `cd ../10_linux_led && make rbf` |
| Client times out | Server not running | Check `ps | grep led_server` on board |
| Client times out | Firewall / network | Ping board first; check `ip addr` |
| LEDs don't change | FPGA not programmed | Check `dmesg | grep fpga_load` |
| `led_server: cannot open /dev/mem` | Not running as root | `sudo led_server` or check init script |

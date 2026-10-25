# 18_gige_camera — DE10-Nano + D5M as a GigE Vision Camera

This project turns the DE10-Nano and the TRDB-D5M sensor board into a
GenICam-described GigE Vision camera. It is planned and run like a small
startup's first product. The full plan (target datasheet, architecture,
milestones M0–M6, risks) is in
[`docs/plans/2026-10-25-gige-vision-camera.md`](../../docs/plans/2026-10-25-gige-vision-camera.md).

| Milestone | Status |
|---|---|
| **M0: Throughput spike** | Done 2026-10-25: [ADR-001](adr/ADR-001-gvsp-packetization.md) accepted provisionally; end-to-end loss check pending a ≥1 Gbit/s receiver |
| M1: Walking skeleton (test pattern → GVSP → Aravis, and HDMI) | Not started |
| M2–M6 | Not started |

Architecture decisions are recorded in [`adr/`](adr/).

---

## M0: Can the Cortex-A9 stream GigE Vision?

### Why this comes first

The Gigabit PHY is wired to the HPS, not the FPGA. Every image byte must go
through Linux networking on the 800 MHz Cortex-A9. If Linux cannot send
GVSP-sized UDP packets fast enough, the whole architecture changes (see
[ADR-001](adr/ADR-001-gvsp-packetization.md)). M0 measures this before any
FPGA work.

### The tools

| Tool | Runs on | Does |
|---|---|---|
| `gvsp_blast` | DE10-Nano | Streams a synthetic 2592×1944 frame buffer as UDP packets shaped like GVSP: a 16-byte header plus a slice of the frame, sent scatter-gather with `sendmmsg()`. Reports Mbit/s, packets/s, fps-equivalent, process CPU, and per-core busy/softirq time from `/proc/stat`. |
| `gvsp_sink` | PC | Receives the stream, counts lost and reordered packets from the sequence number, and reports the received rate. |
| `eth_pause` | DE10-Nano | Shows or sets 802.3x flow control (pause frames). The board image has no `ethtool`. |
| `client/m0_sweep.py` | PC | Runs the sink (locally or on `--sink-host`) and the blaster over ssh for each payload size × batch combination. Writes `results/m0-*.jsonl` and prints a table. `--probe-mtu` finds the board's largest MTU. |

Process CPU time alone would hide most of the cost, because the network stack
runs in softirq context. That is why `gvsp_blast` reports system-wide
per-core numbers as well.

### Prerequisites

- The board is booted from an SD card image that has dropbear (ssh/scp), such
  as [`11_ethernet_hps_led`](../../11_ethernet_hps_led) or
  [`17_opcua_hps_led`](../../17_opcua_hps_led). No FPGA design is needed.
- The board and PC are on the same Gigabit link, ideally a direct cable or a
  single unmanaged switch.
- `cvsoc/tools:1.0` Docker image for the ARM build
  (`docker build -t cvsoc/tools:1.0 -f common/docker/Dockerfile.tools common/docker/`).
- **The receiver must sustain ≥ 1 Gbit/s.** A USB 3 adapter on a USB 2.0
  port (or USB 2.0 VM passthrough) tops out around 270–300 Mbit/s. Check with
  `lsusb -t`: the adapter must show `5000M`, not `480M`.
- On the PC, raise the socket receive buffer limit so the PC itself does not
  drop packets:

  ```bash
  sudo sysctl -w net.core.rmem_max=33554432
  ```
- **WSL2**: the default NAT networking hides the PC from the board. Enable
  mirrored networking (`networkingMode=mirrored` in `%UserProfile%\.wslconfig`)
  or run the sink on a native Linux PC. The WSL2 virtual NIC can itself limit
  throughput, so confirm that a slow result is not caused by the PC side.

### Procedure

```bash
cd 18_gige_camera

# 1. Sanity-check the tools on this PC (no board needed).
make test

# 2. What MTU does the board's EMAC/driver accept?
make m0-probe-mtu HOST=192.168.1.100

# 3. Baseline at the standard 1500-byte MTU.
make m0-sweep HOST=192.168.1.100

# 4. Larger packets. The DE10-Nano's EMAC accepts at most MTU 3796
#    (UDP payload 3768); true 9000-byte jumbo frames are not possible.
sudo ip link set <pc-iface> mtu 3796
python3 client/m0_sweep.py --host 192.168.1.100 --board-mtu 3796 \
    --sizes 1472,2048,3768 --batches 1,32
```

### Pause frames: measure the board, not the receiver

By default the link negotiates 802.3x flow control. A receiver that can't
keep up sends pause frames, and the board obeys. The board then looks slow,
with **zero loss and low CPU**. That was the first M0 result: 274 Mbit/s at
25 % CPU, all of it imposed by a USB 2.0 receiver. To see what the board can
do, turn pause off for the measurement and back on afterwards:

```bash
ssh root@<board> 'nohup /tmp/eth_pause eth0 off >/tmp/pause.txt 2>&1 &'  # link renegotiates (~3 s)
make m0-sweep HOST=<board>
ssh root@<board> 'nohup /tmp/eth_pause eth0 on >/tmp/pause.txt 2>&1 &'
```

With pause off, a slow receiver shows up as loss at the sink, and the
blaster's TX numbers are the board's real capability. Cross-check them with
`/sys/class/net/eth0/statistics/tx_bytes` and `tx_dropped`.

### Board behind a jump host

If the board hangs off another machine (ssh `ProxyJump`), run the sink on
that machine, since it holds the board's link. Build the sink there once,
then:

```bash
make m0-sweep HOST=de10nano SINK_HOST=dev-ubuntu \
    SINK_BIN='~/m0spike/software/spike/build/host/gvsp_sink' DEST_IP=192.168.10.1
```

`DEST_IP` is the jump host's address on the board link. Images that use DHCP
on `eth0` get no address on a direct cable with no DHCP server. Add one over
the serial console with `ip addr add 192.168.10.2/24 dev eth0`.

Every MTU or pause change re-opens the EMAC and renegotiates the link for
about 3 s. Run such changes detached (`nohup … &`, as `--probe-mtu` does) so a
dropped ssh session can't leave them half-applied.

Running the blaster by hand also works:

```bash
make deploy HOST=192.168.1.100                      # copies gvsp_blast + eth_pause to /tmp
./software/spike/build/host/gvsp_sink -j &          # on the PC
ssh root@192.168.1.100 /tmp/gvsp_blast -d <pc-ip> -b 32 -t 10 -j
```

### Reading the results

- **TX ≈ RX Mbit/s, loss ≈ 0, and one core near 100 %**: the board sets the
  limit. Compare `fps` against the sensor's ~14 fps at full resolution.
- **TX ≈ RX Mbit/s, loss ≈ 0, but CPU well below 100 % and no `enobufs`**:
  something downstream is throttling the board through pause frames. See
  the next section.
- **TX high, RX lower, loss > 0**: the PC or the switch is dropping packets.
  Check `rmem_max`, the NIC, and WSL2 before blaming the board.
- **CPU % near 100 on one core, high softirq %**: the per-packet cost
  dominates. Larger batches and jumbo frames should help, and the size of
  the gain tells you how much option 2/3 in ADR-001 could buy.
- **`enobufs` > 0 in the JSON**: the transmit queue is full. The EMAC is
  saturated, which is a good sign.

Copy the summary table into [ADR-001](adr/ADR-001-gvsp-packetization.md) and
write its decision and consequences.

### Results (2026-10-25)

Measured with pause off, single sender, kernel counters cross-checked.
Details and the decision are in [ADR-001](adr/ADR-001-gvsp-packetization.md).

| Payload | MTU | TX Mbit/s | Full-res Bayer8 fps | CPU |
|---|---|---|---|---|
| 1472 B, batch 64 | 1500 | 585 | 14.2 | one core at 100 % |
| 3768 B, batch 32 | 3796 | 936 | 23.0 | one core at 100 % |
| 1472 B × 2 senders | 1500 | 942 (L2) | — | both cores at ~98 % |

The board meets the full-res `BayerGR8` ~14 fps target with one core. MTU
3796 roughly doubles throughput per core, and CPU per byte, not the wire, is
the limit.

---

## Layout

```text
18_gige_camera/
├── Makefile                 # M0 targets (help lists them)
├── doc/
│   ├── README.md            # this file
│   └── adr/                 # architecture decision records
├── software/spike/          # gvsp_blast.c, gvsp_sink.c, eth_pause.c, spike_common.h
├── client/
│   ├── test_spike.py        # loopback self-test (make test)
│   └── m0_sweep.py          # board sweep + MTU probe (local or remote sink)
└── results/                 # m0_sweep.py output (JSON lines, one run per line)
```

The directories for later milestones (`hdl/`, `sim/`, `qsys/`, `quartus/`,
`regmap/`, `br2-external/`) are added as each milestone starts.

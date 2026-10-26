# 18_gige_camera — DE10-Nano + D5M as a GigE Vision Camera

This project turns the DE10-Nano and the TRDB-D5M sensor board into a
GenICam-described GigE Vision camera. It is planned and run like a small
startup's first product. The full plan (target datasheet, architecture,
milestones M0–M6, risks) is in
[`docs/plans/2026-10-25-gige-vision-camera.md`](../../docs/plans/2026-10-25-gige-vision-camera.md).

| Milestone | Status |
|---|---|
| **M0: Throughput spike** | Done 2026-10-25: [ADR-001](adr/ADR-001-gvsp-packetization.md) accepted provisionally; end-to-end loss check pending a ≥1 Gbit/s receiver |
| **M1: Walking skeleton** | GigE path done 2026-10-25: FPGA test pattern → DDR → `camd` → GVSP passes Aravis's `arv-test`. HDMI output deferred (no monitor yet), see [M1](#m1-walking-skeleton) |
| **M2: Sensor bring-up** | In progress: D5M → FPGA → DDR → `camd` → GVSP works, data lines verified; image quality and Bayer phase wait for light on the sensor, HDMI for a monitor. See [M2](#m2-sensor-bring-up) |
| M3–M6 | Not started |

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

## M1: Walking skeleton

A GigE Vision camera that streams an FPGA-generated test pattern and that a
standard client accepts, with no sensor yet. It proves the architecture end to
end:

```text
cam_tpg_writer (FPGA) ──F2H AXI bridge──► DDR 0x3C000000, 13-buffer ring
        ▲ CSRs on LW bridge 0xFF200000          │ uncached /dev/mem
        └──────────────── camd --fpga ◄─────────┘
                            │ GVCP :3956 (discovery, registers, XML, heartbeat)
                            ▼ GVSP (leader / payload / trailer)
                     PC: Aravis, client/gev.py
```

### Pieces

| Piece | Where | Notes |
|---|---|---|
| Test-pattern generator + frame writer | `hdl/cam_tpg_writer.vhd` (renamed `cam_frame_writer.vhd` in M2) | 8 pixels/clock at 50 MHz. 64-bit Avalon bursts of 16 beats, never crossing 4 KB. A frame is reported done only after every burst's write response. |
| Platform Designer system | `qsys/hps_system.tcl` | HPS DDR3 settings from `15_ddr3_fpga_hps`, FPGA-to-HPS AXI bridge instead of SDRAM ports ([ADR-008](adr/ADR-008-frame-write-path.md)) |
| Camera daemon | `software/camd/` | GVCP server and GVSP streamer in C. `--sim` generates frames in software; `--fpga` streams the FPGA's buffers. The GenICam XML is embedded in the binary. |
| GenICam description | `software/camd/genicam/cvsoc_camera.xml` | SFNC features: sensor/ROI, `PixelFormat` (`Mono8`, `BayerGR8`), `TestPattern`, acquisition, `AcquisitionFrameRate`, `GevSCPSPacketSize`. Validates against GenApi 1.1. |
| Golden model | `client/tpg_model.py` | One definition of the test pattern, checked against camd's sim source and the RTL |
| Python GigE Vision client | `client/gev.py`, `client/verify_stream.py` | Debugging and byte-exact stream verification |
| Aravis acceptance config | `client/arv-test.cfg` | For `arv-test-0.8` |

### Board prerequisites

1. Boot with `mem=960M` so Linux leaves the top 64 MB of DDR to the frame
   buffers. On the `16_field_update` image this is the `bootargs` line in
   `boot.scr` on the FAT partition (`/dev/mmcblk0p2`); the original is kept as
   `boot.scr.orig`. `camd --fpga` refuses to start if the region overlaps
   Linux RAM.
2. An image with dropbear and the `fpga_load` module (`10`/`11`/`16`/`17`).

### Procedure

```bash
cd 18_gige_camera
make test                   # camd protocol tests, no board
make sim                    # RTL tests with GHDL, SENSOR_WIDTH 640 and 2592
make fpga                   # Platform Designer + Quartus + compressed .rbf (~9 min)
make load-fpga HOST=de10nano
make run-camd HOST=de10nano

# On a PC on the board's link:
python3 client/verify_stream.py --host 192.168.10.2 --frames 4 --fps 3
arv-camera-test-0.8 -n 192.168.10.2 -f 3
arv-test-0.8 -c client/arv-test.cfg -n "cvsoc*"
```

### Viewing the image from another PC

Any GigE Vision viewer must sit on the board's link: GVSP is UDP sent to the
receiver's own address, and the camera only answers on 192.168.10.x. When the
board is cabled to a lab PC (here dev-ubuntu) and you work from another
machine, run Aravis's viewer on the lab PC and forward its window over SSH:

```bash
ssh -X -C dev-ubuntu arv-viewer-0.8
```

Pick "cvsoc DE10-Nano GigE Camera" and press play. Pattern, ROI and pixel
format are editable in the viewer. The warnings `No xauth data` and
`extension "DRI2" missing` are harmless with a forwarded display.

- **WSL2:** works through WSLg (`DISPLAY=:0`), no X server to install.
- **Speed:** a forwarded 5 MP window is heavy. Keep the frame rate at about
  3 fps or less, or view a smaller ROI.

To run the viewer natively on your own PC instead, the PC itself must be on
the camera's network:

1. Connect the board to the PC's LAN or to a spare NIC on the PC, and give the
   board an address on that network.
2. On WSL2, set `networkingMode=mirrored` under `[wsl2]` in
   `%UserProfile%\.wslconfig` and run `wsl --shutdown`. In the default NAT
   mode, UDP from the LAN does not reach WSL.
3. Run `arv-viewer-0.8` locally.

Routing through the lab PC (IP forwarding on dev-ubuntu) also works, but needs
root there.

### Results (2026-10-25)

- **Byte-exact path:** `verify_stream.py` received full-res moving-ramp frames
  (consecutive n = 0, 1, 2, 3) and ROI color bars in `BayerGR8` and `Mono8`,
  each identical to `tpg_model.py`.
- **Aravis 0.8.30:** `arv-camera-test` streamed 36 full-res frames at 3 fps
  with 0 missing frames, 0 missing packets and 0 resend requests. `arv-test`
  passes every applicable test: XML load, sensor size, continuous acquisition
  10/10 at 3.00 Hz and 2.50 Hz, single acquisition, GigE Vision interface and
  stream-channel checks.
- **FPGA:** 703 ALMs (2 %), timing met at 50 MHz (+0.93 ns setup). The writer
  moves about 266 MB/s through the bridge, so a full 5 MP frame takes ~19 ms.
- Rates were limited to 3 fps by the lab receiver (USB 2.0 adapter,
  ~270 Mbit/s), not by the camera.

### Findings along the way

- **`arv-test` 0.8.30 validates "1.1" against the GenApi 1.0 schema**, so a
  1.1 document always fails its Schema test. The XML is checked against 1.1
  in `client/test_camd.py` instead.
- `arv-test`'s acquisition tests wait 500 ms per buffer, so test rates must
  stay well above 2 Hz.
- RTL simulation caught a race where enabling the writer during a frame reset
  `FRAME_COUNT` one frame late.
- **The board's MAC address changes on every boot** (the image doesn't set
  one). A product needs a stable, per-unit MAC; this goes into the M6
  calibration/identity store.

### Open (M1)

- **HDMI output** (test pattern → HDMI, ADV7513 bring-up) needs a monitor to
  verify. It is deferred until one is available.
- ~~The pixel generator is one combinational path. Pipeline it before raising
  the clock for the sensor in M2.~~ Done in M2: the generator is one
  registered word ahead of the writer (worst setup slack +0.93 → +1.57 ns).

---

## M2: Sensor bring-up

The D5M replaces the test pattern as the frame source. `TestPattern` = `Off`
streams the sensor; the FPGA test patterns stay available for verification.

```text
MT9P001 ──JP1──► d5m_capture ──async_fifo──► cam_frame_writer ──F2H AXI──► DDR ring
  ▲ PIXCLK domain (25 MHz)      (Gray pointers)   50 MHz fabric clock            │
  │ I2C, bit-banged from camd (ADR-009)                                         ▼
  └───────────────── camd --fpga: window = ROI, blanking = frame rate ── GVSP ──► PC
```

### Pieces

| Piece | Where | Notes |
|---|---|---|
| Pixel bus capture | `hdl/d5m_capture.vhd` | PIXCLK domain. Samples DOUT/FVAL/LVAL in the I/O registers, keeps DOUT[11:4], packs 8 pixels per word. Every frame ends with a trailer: lines, last line length, line period, OR/AND of all 12-bit pixels, overflow and irregular-line flags. |
| Clock-domain crossing | `hdl/async_fifo.vhd` | 512 × 68-bit dual-clock FIFO, Gray-coded pointers, first-word fall-through |
| Frame writer | `hdl/cam_frame_writer.vhd` | Sensor or test-pattern source. A sensor frame is published only if it has exactly Width × Height pixels; others count in `CaptureBadFrames`. A burst starts only when all its words are ready, so `write` never drops mid-burst. |
| Word format | `hdl/cam_pkg.vhd` | Pixel, start-of-frame, trailer and fill words |
| Sensor driver | `software/camd/mt9p001.c`, `i2c_bitbang.c` | Register map and timing model (row time, blanking, shutter) from the datasheet, unit-tested in `make test`; I2C bit-banged through the writer's `I2C` register ([ADR-009](adr/ADR-009-sensor-i2c.md)) |
| Pin assignments | `quartus/de10_nano_pin_assignments.tcl` | JP1 header pin ↔ D5M signal ↔ FPGA pin, from the TRDB-D5M and DE10-Nano user manuals |
| Bring-up tools | `camd --sensor-test`, `camd --sensor-reg`, `client/grab.py`, `client/soak.py` | Data-line test, register peek/poke, PNG snapshot, long streaming run |

New camera features: `AcquisitionFrameRate`'s maximum follows the ROI (the
sensor at 25 MHz PIXCLK does 3.67 fps at full resolution), and two
diagnostics counters, `CaptureOverflows` and `CaptureBadFrames`.

### Procedure

```bash
make sim                                   # 2 x 16 RTL tests incl. a D5M bus model
make test                                  # + MT9P001 timing-model unit test
make fpga && make load-fpga HOST=de10nano
make sensor-test HOST=de10nano             # all 12 data lines, glitch count
make run-camd HOST=de10nano                # camd --fpga [--exposure-us 20000] [--gain 2.0]

# On a PC on the board's link:
python3 client/grab.py --host 192.168.10.2 --fps 3 --out frame.png
python3 client/soak.py --host 192.168.10.2 --duration 3600
```

`camd --fpga` starts the sensor and measures its minimum horizontal blank
from one full-width frame before it serves GVCP:

```text
camd: MT9P001 chip version 0x1801
camd: sensor frame 2592x1944, line 3492 PIXCLKs, frame 272.8 ms
camd: HBmin 450, full-res max 3.67 fps at 25 MHz PIXCLK
```

### Results (2026-10-26)

- **Wiring:** the sensor answers on I2C (chip version 0x1801), and frames
  arrive with exactly the configured size. `camd --sensor-test` passes:
  OR = AND = the programmed value for all-zeros, all-ones, 0x555, 0xAAA and
  walking ones, 20 frames each, no glitched frames.
- **Timing model:** with the power-up defaults the measured line is 3492
  PIXCLKs and the frame 275.2 ms. The model predicts LROW = 1296 + 450 and
  (1944 + 26) × 3492 / 25 MHz = 275.1 ms. HBmin measured 450, as in later
  datasheets (Rev C's table says 782).
- **Aravis:** `arv-test` passes with `TestPattern` = `Off`, i.e. with the
  sensor pacing the frames: continuous acquisition 10/10 at 3.00 Hz and
  2.50 Hz, single acquisition, GigE Vision checks. The FPGA test patterns
  are still byte-exact (`verify_stream.py`).
- **Soak (`client/soak.py`):** 25 minutes at full resolution and the
  sensor's maximum rate (3.665 fps, gain 15 for maximum data-line activity):
  5497 frames complete, 0 incomplete, 0 missing, 0 capture overflows,
  0 bad frames. The planned hour was cut short; an earlier attempt was
  interrupted by the lab USB NIC dropping its link (R9), which `soak.py` now
  rides out.
- **FPGA:** 1,263 ALMs (3 %), the capture FIFO in 4 M10K blocks. Timing met:
  fabric +1.57 ns setup slack, PIXCLK (constrained at 25 MHz) +14.5 ns.
- **Image:** the stream works end to end (full resolution, 0 overflows,
  0 bad frames), but the lab was dark: even at 400 ms and 8× gain the mean is
  the black level (10 of 255). Image content and the Bayer phase are still to
  be checked with light on the sensor.

### Findings along the way

- **The data lines glitch at the sensor's default output slew rate.** With
  the color field at 0xFFF (all 12 lines switching together) every frame had
  a line one pixel too long: switching noise corrupted LINE_VALID or PIXCLK
  (plan risk R3), even at 25 MHz. A sweep of R0x07 bits 12:10 (DOUT slew,
  undocumented in Rev C) against bits 9:7 (PIXCLK slew), 4 frames each:

  | DOUT slew \ PIXCLK slew | 0 | 1 | 2 | 3 | 7 |
  |---|---|---|---|---|---|
  | 0 | 0/4 | 0/4 | 0/4 | 0/4 | 0/4 |
  | 1 | 4/4 | 0/4 | 4/4 | 4/4 | 4/4 |
  | 2, 3, 7 | 4/4 | 4/4 | 4/4 | 4/4 | 4/4 |

  camd runs the sensor at DOUT slew 0 (R0x07 = 0x0382). Re-check this before
  raising PIXCLK.
- **The sensor's test pattern is invisible with Row BLC on.** It enters the
  data path before the digital black-level correction, which subtracts the
  dark-column average (the pattern itself) and adds Row_Black_Target: every
  value read 0x0A8. `--sensor-test` turns Row BLC and the black offset off
  while the pattern runs.
- The writer's frame checks (exact size, published only after a trailer)
  drop the frames the sensor sends while it reconfigures, so a ROI change
  never reaches the client as a torn frame.

### Open (M2)

- **Light:** a frame of a real scene to check the image and confirm the
  Bayer phase (expected GR from the datasheet; `client/grab.py` prints the
  CFA channel means and saves a debayered PNG).
- **HDMI passthrough:** needs a monitor (as in M1).
- **Faster PIXCLK:** the sensor PLL can reach ~96 MHz (14 fps full
  resolution). Needs XCLKIN within the PLL's 6–27 MHz input range (25 MHz
  is), the SDC tightened, the capture timing re-checked, and the slew sweep
  repeated.

---

## Layout

```text
18_gige_camera/
├── Makefile                 # all targets (make help)
├── doc/
│   ├── README.md            # this file
│   └── adr/                 # architecture decision records
├── hdl/                     # cam_frame_writer.vhd, d5m_capture.vhd, async_fifo.vhd,
│                            # cam_pkg.vhd, de10_nano_top.vhd
├── qsys/                    # hps_system.tcl, cam_frame_writer_hw.tcl
├── quartus/                 # project/pin/timing TCL, Makefile (Quartus flow)
├── sim/                     # cocotb tests (run.py, test_cam_frame_writer.py)
├── software/
│   ├── camd/                # GigE Vision camera daemon + GenICam XML
│   └── spike/               # M0: gvsp_blast.c, gvsp_sink.c, eth_pause.c
├── client/
│   ├── gev.py               # minimal GVCP/GVSP client
│   ├── tpg_model.py         # test-pattern golden model
│   ├── test_camd.py         # camd protocol tests (make test)
│   ├── verify_stream.py     # byte-exact stream check against a live camera
│   ├── grab.py              # save one frame as PNG, print CFA channel means
│   ├── soak.py              # long streaming run: capture and network losses
│   ├── arv-test.cfg         # Aravis acceptance configuration
│   ├── test_spike.py        # M0 loopback self-test
│   └── m0_sweep.py          # M0 board sweep + MTU probe
└── results/                 # m0_sweep.py output (JSON lines, one run per line)
```

Directories for later milestones (`regmap/`, `br2-external/`) are added as
each milestone starts.

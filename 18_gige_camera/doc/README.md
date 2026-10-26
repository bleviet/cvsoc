# 18_gige_camera — DE10-Nano + D5M as a GigE Vision Camera

This project turns the DE10-Nano and the TRDB-D5M sensor board into a
GenICam-described GigE Vision camera. It is planned and run like a small
startup's first product. The full plan (target datasheet, architecture,
milestones M0–M6, risks) is in
[`docs/plans/2026-10-25-gige-vision-camera.md`](../../docs/plans/2026-10-25-gige-vision-camera.md).

| Milestone | Status |
|---|---|
| **M0: Throughput spike** | Done 2026-10-25: [ADR-001](adr/ADR-001-gvsp-packetization.md) accepted provisionally; end-to-end loss check pending a ≥1 Gbit/s receiver |
| **M1: Walking skeleton** | GigE path done 2026-10-25: FPGA test pattern → DDR → `camd` → GVSP passes Aravis's `arv-test`. HDMI output moved to M2, see [M1](#m1-walking-skeleton) |
| **M2: Sensor bring-up** | In progress: D5M → FPGA → DDR → `camd` → GVSP works, data lines verified; HDMI preview works (720p60). Upright image, Bayer phase confirmed, auto exposure and white balance. See [M2](#m2-sensor-bring-up) |
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

- ~~**HDMI output** (test pattern → HDMI, ADV7513 bring-up) needs a monitor to
  verify.~~ Done in M2, see [HDMI preview](#hdmi-preview).
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
| Pixel bus capture | `hdl/d5m_capture.vhd` | PIXCLK domain. Samples DOUT/FVAL/LVAL in the I/O registers, keeps DOUT[11:4], packs 8 pixels per word. Trims one pixel on every side for the mirrored readout. Every frame ends with a trailer: lines, last line length, line period, OR/AND of all 12-bit pixels, overflow and irregular-line flags. |
| Clock-domain crossing | `hdl/async_fifo.vhd` | 512 × 68-bit dual-clock FIFO, Gray-coded pointers, first-word fall-through |
| Frame writer | `hdl/cam_frame_writer.vhd` | Sensor or test-pattern source. A sensor frame is published only if it has exactly Width × Height pixels; others count in `CaptureBadFrames`. A burst starts only when all its words are ready, so `write` never drops mid-burst. |
| Word format | `hdl/cam_pkg.vhd` | Pixel, start-of-frame, trailer and fill words |
| Sensor driver | `software/camd/mt9p001.c`, `i2c_bitbang.c` | Register map and timing model (row time, blanking, shutter) from the datasheet, unit-tested in `make test`; I2C bit-banged through the writer's `I2C` register ([ADR-009](adr/ADR-009-sensor-i2c.md)) |
| Pin assignments | `quartus/de10_nano_pin_assignments.tcl` | JP1 header pin ↔ D5M signal ↔ FPGA pin, from the TRDB-D5M and DE10-Nano user manuals |
| Bring-up tools | `camd --sensor-test`, `camd --sensor-reg`, `client/grab.py`, `client/soak.py` | Data-line test, register peek/poke, PNG snapshot, long streaming run |
| Auto exposure | `software/camd/ae.c` | Frame mean → exposure, then gain; unit-tested in closed loop (see [Auto exposure](#auto-exposure)) |
| HDMI preview | `hdl/hdmi_preview.vhd`, `software/camd/adv7513.c` | The latest frame on HDMI, 720p60 (see [HDMI preview](#hdmi-preview)) |

New camera features: `AcquisitionFrameRate`'s maximum follows the ROI (the
sensor at 25 MHz PIXCLK does 3.66 fps at full resolution), and two
diagnostics counters, `CaptureOverflows` and `CaptureBadFrames`.

### Procedure

```bash
make sim                                   # 2 x 17 writer tests incl. a D5M bus model, 7 HDMI tests
make test                                  # + MT9P001 timing-model and auto-exposure unit tests
make fpga && make load-fpga HOST=de10nano
make sensor-test HOST=de10nano             # all 12 data lines, glitch count
make run-camd HOST=de10nano                # camd --fpga [--ae-target 110] [--exposure-us N --gain G]

# On a PC on the board's link:
python3 client/grab.py --host 192.168.10.2 --fps 3 --out frame.png
python3 client/soak.py --host 192.168.10.2 --duration 3600
```

`camd --fpga` starts the sensor and measures its minimum horizontal blank
from one full-width frame before it serves GVCP:

```text
camd: MT9P001 chip version 0x1801
camd: sensor frame 2592x1944, line 3494 PIXCLKs, frame 273.2 ms
camd: HBmin 450, full-res max 3.66 fps at 25 MHz PIXCLK
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
- **Soak (`client/soak.py`):** 35 minutes at full resolution and the
  sensor's maximum rate (3.665 fps, gain 15 for maximum data-line activity):
  7696 frames complete, 0 incomplete, 0 missing, 0 capture overflows,
  0 bad frames. The planned hour was cut short; an earlier attempt was
  interrupted by the lab USB NIC dropping its link (R9), which `soak.py` now
  rides out.
- **FPGA:** 1,263 ALMs (3 %), the capture FIFO in 4 M10K blocks. Timing met:
  fabric +1.57 ns setup slack, PIXCLK (constrained at 25 MHz) +14.5 ns.
- **Image:** the stream works end to end (full resolution, 0 overflows,
  0 bad frames). With light on the sensor (lab, room lighting), the mean of
  the central 640 × 480 grows linearly with exposure × gain until it clips:

  | Exposure × gain | 1 ms × 1 | 20 ms × 2 | 100 ms × 4 | 272 ms × 8 |
  |---|---|---|---|---|
  | Mean (black = 10) | 10.1 | 14.0 | 48.5 | 210.9 (clipping) |

  The fixed 20 ms × 2 default was far too dark for the room, hence
  [auto exposure](#auto-exposure). The two greens agree within 1 % in every
  frame (e.g. G1 230.6, G2 231.3), so green is on the diagonal of each 2x2
  cell. Raising only the sensor's `Red_Gain` (×3) raised only the pixels
  right of the first G (27.5 → 61.2, the rest unchanged): the phase is
  **GR**, also with the mirrored readout (see
  [Orientation and white balance](#orientation-and-white-balance)).

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
- **Mirroring changes the Bayer phase, and the window cannot fix it.**
  Row_Start/Column_Start must be even and the sizes odd (the sensor ignores
  the low bit; a window shifted by one gave identical frames), so a readout
  mirrored in both directions always starts on an odd row and column:
  G B / R G. Found when the first white balance ran away: it raised the
  red gain, and the pixels it measured as red got darker.
- The writer's frame checks (exact size, published only after a trailer)
  drop the frames the sensor sends while it reconfigures, so a ROI change
  never reaches the client as a torn frame.

### Auto exposure

camd adjusts exposure and gain so the frame mean (8-bit) sits at a target,
110 by default (mid-grey on a monitor), measured on the frames in DDR:

- **Statistics:** 4096 2x2 cells on a grid over the latest frame, read from
  uncached DDR in a few milliseconds. Mean and the fraction of cells with a
  clipped pixel (≥ 250).
- **Control:** the sensor is linear above its black level (10), so one step
  scales exposure × gain by (target − 10) / (mean − 10), at most 8× per step,
  with a ±8 % dead band. Exposure goes up first, to the frame time
  (272 ms at full resolution); gain (noise) only after that, up to 15.75. A
  frame with over 25 % clipped cells steps down at least 2×; a few clipped
  highlights, such as a lamp or a window, are normal at the right exposure.
- **Sensor:** only shutter width and gain change, synchronized to a frame
  start and without a restart, so no frame is torn or dropped. The next 3
  frames are skipped before measuring again.
- It runs while streaming and in the HDMI preview, on sensor frames only.
  `--exposure-us` or `--gain` switch to fixed values; `--ae-target N`
  makes the image brighter or darker.

camd logs the way there and where it settles. In the lab, from a mean of 15:

```text
camd: AE adjusting: mean 15 (target 110, 0.0 % clipped), exposure 272624 us, gain 1.17
camd: AE settled: mean 108 (target 110, 9.9 % clipped), exposure 272624 us, gain 3.11
```

The settled values are a good start for fixed settings
(`--exposure-us 272000 --gain 3.1`). `software/camd/test_ae.c` (`make test`)
closes the loop with a model of the sensor: the lab scene, the lab with a
lamp in view, daylight 1000× brighter, and darkness.

### Focusing the lens

Exposure does not bring back detail that the lens blurs. A 1:1 raw crop of
the first well-exposed frames showed the Bayer checkerboard sharp to the
pixel (the data path is fine) but a box edge spread over ~150 pixels: the
lens was far out of focus, or the object closer than the lens can focus.

`camd --focus` logs a focus measure once a second: the RMS difference
between neighbouring 2x2 cells in the central half of the frame, in percent
of the mean brightness. Turn the lens slowly while watching it, go past the
peak, and come back to it. The value depends on the scene, so only compare
readings of the same view; it pauses while the auto exposure adjusts
(noise counts in it and depends on gain). Out of focus in the lab it read
1.93 ± 0.03 over a minute, so a change of a few percent is real.

```bash
ssh de10nano 'killall camd; sleep 0.5; nohup /tmp/camd --fpga --focus --hdmi grey \
    >/tmp/camd.log 2>&1 &'
ssh de10nano tail -f /tmp/camd.log   # camd: focus   2.41  (best   2.63)
```

### Orientation and white balance

- **Upright:** the lens projects the scene upside down, so the sensor reads
  its rows and columns in reverse (Read_Mode_2 bits 15 and 14, as Terasic's
  demos do). To keep BayerGR8, camd asks for a window one pixel larger on
  every side and `d5m_capture` drops the first and last line and column
  (`SENSOR_CTRL` bit 4, trim): the first pixel kept is a G1 again. The rest
  of the system (writer, test patterns, HDMI, GenICam, clients) is unchanged.
  Offsets count from the top left of the upright image. The two extra
  columns and rows add 2 PIXCLKs per line and 2 lines per frame
  (3494 PIXCLKs, 3.66 fps at full resolution).
- **White balance (grey world):** the sensor has a gain per CFA color
  (R0x2B–R0x2E). camd sets red and blue relative to green so the unclipped
  parts of the frame average to grey, in the same loop as the auto exposure
  and with a ±4 % dead band (the gain steps are 1/8). On by default;
  `--wb R,B` fixes the ratios. A scene dominated by one color (a red wall)
  fools it.

In the lab (white ceiling, room light) it settles in 3 steps:

```text
camd: AE settled: mean 117 (target 110, 1.5 % clipped), exposure 273059 us, gain 2.21, WB R 1.20 B 1.60 (G 116 R 112 B 116)
```

Before white balance the same ceiling read G 127, R 113, B 81: yellow-green.

### HDMI preview

For aiming and focusing without a PC, the HDMI port shows the camera's
latest frame at 1280 × 720, 60 Hz:

```text
cam_frame_writer ──DDR ring──► hdmi_preview ──► ADV7513 ──► monitor
   (last completed frame)      line fetcher (50 MHz, F2H AXI read)
                               2-row ping-pong buffers ──► 720p timing (74.25 MHz)
```

- **1:1 pixels, centered.** A full-resolution frame shows its central
  1280 × 720 pixels, which is what focusing needs. To look elsewhere, set
  `Width`/`Height` to 1280 × 720 and move `OffsetX`/`OffsetY` (in Aravis).
  Smaller frames are centered with black borders.
- **Color** from each 2x2 GR/BG cell (no interpolation), or the raw values in
  grey with `camd --hdmi grey` (sharper for focusing), or the FPGA's color
  bars with `--hdmi bars` (checks the HDMI path alone).
- **Always live:** while no GigE client streams, camd keeps the sensor
  running with the current settings, so the monitor shows a live image. A
  client's ROI and frame rate apply to the preview too.
- camd configures the ADV7513 over I2C (bit-banged like the sensor's) with
  the register values of Terasic's HDMI_TX demo, and again whenever a
  monitor is plugged in.
- The pixel clock comes from a fractional PLL (exactly 74.25 MHz). The
  fetcher reads 2 sensor rows per 2 display lines (about 55 MB/s);
  `hdmi_preview`'s `UNDERFLOWS` register counts row pairs that were late.

#### Controlling the preview over ssh

camd sets the preview up when it starts; afterwards the `hdmi_preview`
registers (LW bridge 0xFF200100) can be poked directly with `devmem`. camd
writes `CTRL` again only when it restarts (and clears it when it exits), and
re-centers `SRC_X`/`SRC_Y` whenever it restarts the writer, e.g. on a ROI change.

| Register | Address | Contents |
|---|---|---|
| `ID` | 0xFF200100 | 0x48444D31 ("HDM1") |
| `CTRL` | 0xFF200104 | bit 0 show (0: black), bit 1 grey, bit 2 color bars |
| `STATUS` | 0xFF200108 | bit 0: a frame is available |
| `SRC_X` / `SRC_Y` | 0xFF20010C / 0xFF200110 | top-left frame pixel shown (signed; X a multiple of 8, Y even) |
| `FRAMES` | 0xFF200114 | display frames sent (counts at 60 Hz) |
| `UNDERFLOWS` | 0xFF200118 | row pairs fetched too late |

```bash
# Start camd with a preview mode (default color) and auto exposure ...
ssh de10nano 'killall camd; sleep 0.5; nohup /tmp/camd --fpga --hdmi grey \
    >/tmp/camd.log 2>&1 &'
# ... or a brighter auto exposure, or fixed exposure and gain.
ssh de10nano 'killall camd; sleep 0.5; nohup /tmp/camd --fpga --ae-target 140 \
    >/tmp/camd.log 2>&1 &'
ssh de10nano 'killall camd; sleep 0.5; nohup /tmp/camd --fpga \
    --exposure-us 100000 --gain 4 >/tmp/camd.log 2>&1 &'
ssh de10nano cat /tmp/camd.log     # "HDMI monitor connected", "AE settled: ..."

# Switch the mode without restarting camd.
ssh de10nano devmem 0xFF200104 32 1           # live, color
ssh de10nano devmem 0xFF200104 32 3           # live, grey (raw values)
ssh de10nano devmem 0xFF200104 32 5           # color bars
ssh de10nano devmem 0xFF200104 32 0           # black

# Pan within a full-resolution frame (camd centers it at 656, 612).
ssh de10nano 'devmem 0xFF20010C 32 0; devmem 0xFF200110 32 0'   # top-left corner

# Health: FRAMES must grow by about 60 per second, UNDERFLOWS stay 0.
ssh de10nano 'devmem 0xFF200114 32; sleep 1; devmem 0xFF200114 32; devmem 0xFF200118 32'
```

`make run-camd HOST=de10nano` restarts camd with the defaults (color).

### Open (M2)

- **Focus:** better after a first turn by hand (focus measure 1.9 → 2.8),
  still soft; fine-tune with `camd --focus`.
- **Gamma:** the HDMI preview shows linear values, so shadows look nearly
  black. A gamma curve belongs to the M4 ISP; a LUT in `hdmi_preview` would
  help aiming before that.
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
│                            # cam_pkg.vhd, hdmi_preview.vhd, de10_nano_top.vhd
├── qsys/                    # hps_system.tcl, cam_frame_writer_hw.tcl, hdmi_preview_hw.tcl
├── quartus/                 # project/pin/timing TCL, Makefile (Quartus flow)
├── sim/                     # cocotb tests (run.py, test_cam_frame_writer.py,
│                            # test_hdmi_preview.py)
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

# ADR-001: Where GVSP packets are built

- **Status**: Accepted, provisionally. Revisit in M5 with an end-to-end receiver (see Open items).
- **Date**: 2026-10-25
- **Plan**: [GigE Vision camera product plan](../../../docs/plans/2026-10-25-gige-vision-camera.md), §3 and §11

## Context

The DE10-Nano Gigabit PHY is wired to the HPS EMAC, so every image byte leaves
through Linux networking on an 800 MHz dual Cortex-A9. Full-resolution
`BayerGR8` at the sensor's ~14 fps needs ~565 Mbit/s of UDP payload, and
`BayerGR12p` needs ~850 Mbit/s.

Options for building GVSP packets:

1. **Linux userspace** (`camd` sends from a mapped frame buffer with
   `sendmmsg`, scatter-gather header + frame pointer). Simplest and most
   debuggable.
2. **Linux userspace with zero-copy** (`MSG_ZEROCOPY`, dmabuf from the V4L2
   driver). Same design, less memcpy in the kernel.
3. **FPGA-built packets**: the fabric writes complete Ethernet/IP/UDP/GVSP
   frames into DDR, and a kernel driver only chains EMAC DMA descriptors.
   Fastest, but it bypasses the network stack (ARP, routing, checksums) and is
   much harder to build and debug.

## M0 Measurements (2026-10-25)

### Setup

| Item | Value |
|---|---|
| Board image | `11_ethernet_hps_led` SD card, kernel 6.6.86, `socfpga-dwmac` (dwmac1000), KSZ9031 PHY |
| Link | 1 Gbit/s full duplex, direct cable to dev-ubuntu |
| Receiver | dev-ubuntu (VM) with an ASIX AX88179 USB 3 adapter **enumerated at USB 2.0 (480 Mbit/s)** |
| Largest MTU accepted by `eth0` | **3796** (max UDP payload 3768 B). The driver rejects more with "mtu greater than device maximum", consistent with the 4 KB TX FIFO. True 9000-byte jumbo frames are impossible. |
| Flow control | Link negotiates rx/tx pause by default; driver pause time 65535 |
| Tools | `gvsp_blast` (static ARM), `gvsp_sink`, `eth_pause`, `client/m0_sweep.py` |
| Raw data | `results/m0-20261025-*.jsonl` |

### Finding 1: The receiver was throttling the board through pause frames

With default flow control, the board sent **274 Mbit/s with 0 % loss, both
cores only ~25 % busy, and no ENOBUFS**. That's backpressure: the USB 2.0
adapter sends 802.3x pause frames and the EMAC obeys. With pause turned off on
the board (`eth_pause eth0 off`), the same run reached **569 Mbit/s**, and the
receiver dropped 79 %. All board-side numbers below were taken with pause off.
They measure what the board can transmit, verified against the kernel's
`eth0` `tx_bytes` / `tx_dropped` counters (0 dropped, 0 errors).

### Finding 2: Board transmit capability (option 1, single sender)

| Payload (B) | MTU | Batch | TX Mbit/s | kpkt/s | Frames/s (2592×1944 Bayer8) | Busy cores |
|---|---|---|---|---|---|---|
| 1472 | 1500 | 1 | 491 | 41.7 | 12.0 | 1 of 2 at 100 % |
| 1472 | 1500 | 8 | 558 | 47.4 | 13.6 | 1 of 2 at 100 % |
| 1472 | 1500 | 32 | 569 | 48.3 | 13.8 | 1 of 2 at 100 % |
| 1472 | 1500 | 64 | 585 | 49.7 | 14.2 | 1 of 2 at 100 % |
| 2048 | 3796 | 1 | 620 | 37.9 | 15.2 | 1 of 2 at 100 % |
| 2048 | 3796 | 32 | 686 | 41.9 | 16.8 | 1 of 2 at 100 % |
| 3768 | 3796 | 1 | 849 | 28.2 | 20.8 | 1 of 2 at 100 % |
| 3768 | 3796 | 32 | **936** | 31.1 | **23.0** | 1 of 2 at 100 % |

Cross-check for 3768 B / batch 32: the kernel counted 941 Mbit/s at L2 with 0
dropped, which is about 95 % of GigE line rate.

### Finding 3: Two senders at MTU 1500

Two concurrent `gvsp_blast` processes at 1472 B / batch 32 reached **942 Mbit/s
at L2 (line rate), 0 dropped, but both cores were ~98 % busy**. The split was
uneven (625 + 292 Mbit/s), consistent with contention on the single TX queue.

## Decision

1. **Option 1 (Linux userspace `sendmmsg`, scatter-gather) is the baseline for
   M1–M4.** It already meets the headline mode: full-res `BayerGR8` at ~14 fps
   needs ~565 Mbit/s, and one core delivers 569–585 Mbit/s at MTU 1500.
2. **Default GVSP packet size: 3768 bytes (`GevSCPSPacketSize`), with MTU 3796
   on both ends as a documented system requirement.** It nearly doubles
   throughput per core (936 vs 569 Mbit/s) and is the only way to reach the
   `BayerGR12p` full-res mode (~850 Mbit/s) on a single core. 1472 bytes
   remains the fallback for PCs that can't raise their MTU, with the
   datasheet frame rate de-rated accordingly.
3. **Option 2 (zero-copy) is the M5 performance work.** In every run the
   limit was one CPU core, not the wire, so cutting per-byte cost is where
   the headroom comes from. Using the second core for a parallel sender is
   *not* the plan: it reaches line rate but leaves no CPU for GVCP, 3A, PTP,
   or OTA.
4. **Option 3 (FPGA-built packets) is not needed** for the v1.0 datasheet.
   Revisit only if option 2 can't free a meaningful share of a core at
   full-res `BayerGR12p`.

## Consequences

- At the target modes, `camd`'s streaming thread will own one core. Pin it
  (CPU affinity) and keep GVCP, 3A, and housekeeping on the other core. The
  plan's CPU budget must assume one core for everything that is not streaming.
- The datasheet lists two frame-rate columns: MTU ≥ 3796 and MTU 1500.
- GVCP must implement `GevSCPSPacketSize` and the test-packet mechanism
  (`GevSCPSFireTestPacket`, `GevSCPSDoNotFragment`), so clients can discover
  the usable packet size.
- **Lab requirement:** the test receiver must sustain ≥ 1 Gbit/s. The current
  dev-ubuntu AX88179 runs at USB 2.0 and caps at ~274 Mbit/s. Move it to a
  USB 3 port / USB 3 passthrough or use a PCIe NIC, and raise its MTU to 3796.
- **Pause frames are a trap.** A slow receiver silently throttles the camera
  instead of losing packets. `camd` should log negotiated flow control at
  stream start. The M0 procedure measures with pause off and turns it back on
  afterwards.

## Open items (must close before M5 ends)

1. **End-to-end loss above 274 Mbit/s is unverified**, because the receiver
   was the bottleneck. Repeat the sweep with a ≥1 Gbit/s receiver at MTU 3796
   and confirm <0.01 % loss.
2. **Frame-buffer memory type.** M0 used a cached `malloc` buffer. The real
   frame buffer is written by the FPGA over the F2H SDRAM bridge. If it is
   mapped uncached, the kernel's copy during `sendmsg` will be much slower.
   M1 must measure `gvsp_blast` against a reserved-memory / dma-buf buffer
   with explicit cache maintenance before trusting these numbers.
3. The measurements ran beside the image's `led_server`. Repeat on the
   `18_gige_camera` image once it exists.

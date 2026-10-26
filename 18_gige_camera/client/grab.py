#!/usr/bin/env python3
"""Grab one frame from camd and save it as a PNG (no numpy needed).

BayerGR8 frames are demosaiced the simplest way: each 2x2 cell (G R / B G)
becomes one RGB pixel, so the PNG is half the width and height. Mono8
frames are saved as they are. The script also prints the mean of each CFA
channel: in a BayerGR8 frame the two greens should agree with each other
(and usually differ from red and blue), which confirms the Bayer phase.

  python3 client/grab.py --host 192.168.10.2 --out frame.png
  python3 client/grab.py --host 192.168.10.2 --pattern 2 --out bars.png
"""

import argparse
import socket
import struct
import sys
import zlib

import gev


def png(path, width, height, rows, channels):
    """Write 8-bit grey (channels=1) or RGB (channels=3) rows as a PNG."""
    def chunk(tag, data):
        c = tag + data
        return struct.pack(">I", len(data)) + c + struct.pack(">I", zlib.crc32(c) & 0xFFFFFFFF)

    color = 0 if channels == 1 else 2
    raw = b"".join(b"\0" + bytes(r) for r in rows)
    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n")
        f.write(chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, color, 0, 0, 0)))
        f.write(chunk(b"IDAT", zlib.compress(raw, 6)))
        f.write(chunk(b"IEND", b""))


def bayer_gr_to_rgb(data, w, h):
    """One RGB pixel per 2x2 GR/BG cell; also returns the channel means."""
    rows = []
    sums = dict(G1=0, R=0, B=0, G2=0)
    for y in range(0, h - 1, 2):
        top = data[y * w:(y + 1) * w]
        bot = data[(y + 1) * w:(y + 2) * w]
        g1, r, b, g2 = top[0::2], top[1::2], bot[0::2], bot[1::2]
        sums["G1"] += sum(g1)
        sums["R"] += sum(r)
        sums["B"] += sum(b)
        sums["G2"] += sum(g2)
        row = bytearray(3 * len(r))
        row[0::3] = r
        row[1::3] = bytes((a + c) >> 1 for a, c in zip(g1, g2))
        row[2::3] = b
        rows.append(row)
    cells = (w // 2) * (h // 2)
    return rows, {k: v / cells for k, v in sums.items()}


def main():
    p = argparse.ArgumentParser(description=__doc__,
                                formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--host", required=True)
    p.add_argument("--port", type=int, default=gev.GVCP_PORT, help="GVCP port")
    p.add_argument("--out", default="frame.png")
    p.add_argument("--pattern", type=int, default=0, help="TestPattern (0 = sensor)")
    p.add_argument("--fps", type=float, help="AcquisitionFrameRate (default: keep)")
    p.add_argument("--roi", help="WIDTHxHEIGHT+X+Y (default: full sensor)")
    p.add_argument("--mono", action="store_true", help="PixelFormat Mono8")
    p.add_argument("--skip", type=int, default=1,
                   help="frames to discard first (exposure settling)")
    p.add_argument("--packet-size", type=int, default=1500)
    args = p.parse_args()

    probe = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    probe.connect((args.host, args.port))
    local_ip = probe.getsockname()[0]

    cam = gev.Gvcp(args.host, args.port)
    reg = gev.REG
    cam.take_control()
    try:
        sensor_w, sensor_h = cam.read_reg(reg["WIDTH_MAX"]), cam.read_reg(reg["HEIGHT_MAX"])
        w, h, ox, oy = sensor_w, sensor_h, 0, 0
        if args.roi:
            size, ox, oy = args.roi.split("+")
            w, h = (int(v) for v in size.split("x"))
            ox, oy = int(ox), int(oy)
        fmt = gev.MONO8 if args.mono else gev.BAYER_GR8
        for name, value in (("OFFSET_X", 0), ("OFFSET_Y", 0), ("WIDTH", w), ("HEIGHT", h),
                            ("OFFSET_X", ox), ("OFFSET_Y", oy), ("PIXEL_FORMAT", fmt),
                            ("TEST_PATTERN", args.pattern)):
            cam.write_reg(reg[name], value)
        if args.fps:
            cam.write_float(reg["FRAME_RATE"], args.fps)
        fps = cam.read_float(reg["FRAME_RATE"])
        rx = gev.Gvsp()
        try:
            gev.start_stream(cam, rx, local_ip, args.packet_size)
            frames = rx.frames(args.skip + 1, timeout=10 + (args.skip + 1) / fps)
        finally:
            gev.stop_stream(cam)
            rx.close()
        overflows = cam.read_reg(reg["CAPTURE_OVERFLOWS"])
        bad = cam.read_reg(reg["CAPTURE_BAD_FRAMES"])
    finally:
        cam.release()

    f = frames[-1]
    if not f["complete"]:
        print("frame incomplete (packets lost)", file=sys.stderr)
        return 1
    data = f["data"]
    print(f"block {f['block_id']}: {w}x{h}+{ox}+{oy} at {fps:.2f} fps, "
          f"mean {sum(data) / len(data):.1f}, min {min(data)}, max {max(data)}; "
          f"capture overflows {overflows}, bad frames {bad}")
    if fmt == gev.BAYER_GR8:
        rows, means = bayer_gr_to_rgb(data, w, h)
        print("CFA means: " + ", ".join(f"{k} {v:.1f}" for k, v in means.items()))
        png(args.out, w // 2, h // 2, rows, 3)
    else:
        png(args.out, w, h, [data[y * w:(y + 1) * w] for y in range(h)], 1)
    print(f"wrote {args.out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())

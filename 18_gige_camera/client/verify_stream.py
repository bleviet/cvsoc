#!/usr/bin/env python3
"""Grab frames from a running camd and check them against the test-pattern model.

Works for both camd --sim and camd --fpga, so it verifies the whole M1 path:
FPGA generator -> DDR -> camd -> GVSP -> this PC.

  python3 client/verify_stream.py --host 192.168.10.2 --frames 5 --fps 3
"""

import argparse
import socket
import sys

import gev
import tpg_model


def grab_and_check(cam, local_ip, args, pattern, fmt, w, h, ox, oy):
    reg = gev.REG
    for name, value in (("OFFSET_X", 0), ("OFFSET_Y", 0), ("WIDTH", w), ("HEIGHT", h),
                        ("OFFSET_X", ox), ("OFFSET_Y", oy), ("PIXEL_FORMAT", fmt),
                        ("TEST_PATTERN", pattern)):
        cam.write_reg(reg[name], value)
    cam.write_float(reg["FRAME_RATE"], args.fps)
    sensor_w = cam.read_reg(reg["WIDTH_MAX"])

    rx = gev.Gvsp()
    try:
        gev.start_stream(cam, rx, local_ip, args.packet_size)
        frames = rx.frames(args.frames, timeout=30 + args.frames / args.fps)
    finally:
        gev.stop_stream(cam)
        rx.close()

    ok = True
    prev = None
    for f in frames:
        n = (f["data"][0] - ox) & 0xFF if pattern == tpg_model.GREY_HRAMP_MOVING else 0
        want = tpg_model.frame(pattern, fmt, w, h, ox, oy, n, sensor_w)
        good = f["complete"] and f["data"] == want
        step = "" if prev is None else f" (+{(n - prev) & 0xFF} frames)"
        print(f"  block {f['block_id']:5} {w}x{h}+{ox}+{oy} fmt 0x{fmt:08x} pattern {pattern} "
              f"n={n:3}{step}: {'OK' if good else 'MISMATCH'}")
        if not good:
            bad = sum(a != b for a, b in zip(f["data"], want)) + abs(len(f["data"]) - len(want))
            print(f"    {bad} bytes differ, complete={f['complete']}")
            ok = False
        prev = n
    return ok


def main():
    p = argparse.ArgumentParser(description=__doc__,
                                formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--host", required=True)
    p.add_argument("--frames", type=int, default=3)
    p.add_argument("--fps", type=float, default=3.0)
    p.add_argument("--packet-size", type=int, default=1500)
    args = p.parse_args()

    probe = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    probe.connect((args.host, gev.GVCP_PORT))
    local_ip = probe.getsockname()[0]

    cam = gev.Gvcp(args.host)
    info = cam.discover()
    print(f"{info['model']} serial {info['serial']} ({info['device_version']}) at {info['ip']}")
    sensor_w = cam.read_reg(gev.REG["WIDTH_MAX"])
    sensor_h = cam.read_reg(gev.REG["HEIGHT_MAX"])

    cam.take_control()
    try:
        results = [
            grab_and_check(cam, local_ip, args, tpg_model.GREY_HRAMP_MOVING, gev.BAYER_GR8,
                           sensor_w, sensor_h, 0, 0),
            grab_and_check(cam, local_ip, args, tpg_model.COLOR_BAR, gev.BAYER_GR8,
                           sensor_w // 2, 200, sensor_w // 4 // 8 * 8, 100),
            grab_and_check(cam, local_ip, args, tpg_model.COLOR_BAR, gev.MONO8,
                           sensor_w // 2, 200, sensor_w // 4 // 8 * 8, 100),
        ]
    finally:
        cam.write_reg(gev.REG["OFFSET_X"], 0)
        cam.write_reg(gev.REG["OFFSET_Y"], 0)
        cam.write_reg(gev.REG["WIDTH"], sensor_w)
        cam.write_reg(gev.REG["HEIGHT"], sensor_h)
        cam.write_reg(gev.REG["TEST_PATTERN"], tpg_model.GREY_HRAMP_MOVING)
        cam.write_reg(gev.REG["PIXEL_FORMAT"], gev.BAYER_GR8)
        cam.release()
    print("PASS" if all(results) else "FAIL")
    return 0 if all(results) else 1


if __name__ == "__main__":
    sys.exit(main())

#!/usr/bin/env python3
"""Stream from camd for a long time and count what goes wrong (M2 exit test).

Two independent sets of numbers:

  camera side   CaptureOverflows (FPGA capture FIFO overflowed) and
                CaptureBadFrames (sensor frames dropped for a wrong size),
                read from the camera every --interval seconds
  network side  blocks received complete / incomplete, and block IDs that
                never arrived (camd skipped them, or their leader was lost)

The M2 exit criterion is zero capture overflows over an hour at the
sensor's full rate; network losses depend on the receiving PC and are
reported separately. The script reads a register every second as the GVCP
heartbeat (camd stops streaming after 3 s of silence). If the link drops
(no ack, or no stream packet for 5 s), it waits, takes control again and
restarts the stream; the restarts are counted, and the camera's counters
are cumulative, so the capture result still covers the whole run.

  python3 client/soak.py --host 192.168.10.2 --duration 3600
"""

import argparse
import socket
import struct
import sys
import time

import gev


def main():
    p = argparse.ArgumentParser(description=__doc__,
                                formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--host", required=True)
    p.add_argument("--port", type=int, default=gev.GVCP_PORT)
    p.add_argument("--duration", type=float, default=3600, help="seconds")
    p.add_argument("--interval", type=float, default=10, help="report period, seconds")
    p.add_argument("--fps", type=float, help="AcquisitionFrameRate (default: its maximum)")
    p.add_argument("--pattern", type=int, default=0, help="TestPattern (0 = sensor)")
    p.add_argument("--packet-size", type=int, default=1500)
    args = p.parse_args()

    probe = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    probe.connect((args.host, args.port))
    local_ip = probe.getsockname()[0]

    cam = gev.Gvcp(args.host, args.port)
    reg = gev.REG
    cam.take_control()
    rx = gev.Gvsp(timeout=0.2)
    complete = incomplete = missing = restarts = 0
    try:
        cam.write_reg(reg["TEST_PATTERN"], args.pattern)
        fps = args.fps or cam.read_float(reg["FRAME_RATE_MAX"])
        cam.write_float(reg["FRAME_RATE"], fps)
        fps = cam.read_float(reg["FRAME_RATE"])
        w, h = cam.read_reg(reg["WIDTH"]), cam.read_reg(reg["HEIGHT"])
        ovf0 = cam.read_reg(reg["CAPTURE_OVERFLOWS"])
        bad0 = cam.read_reg(reg["CAPTURE_BAD_FRAMES"])
        print(f"soak: {w}x{h} pattern {args.pattern} at {fps:.3f} fps for "
              f"{args.duration:.0f} s", flush=True)
        gev.start_stream(cam, rx, local_ip, args.packet_size)

        start = time.monotonic()
        next_report = start + args.interval
        next_heartbeat = start + 1.0
        last_packet = start
        block = payloads = None
        last_block = None
        while True:
            now = time.monotonic()
            try:
                if now >= next_heartbeat:
                    cam.read_reg(gev.BS_CCP)
                    next_heartbeat = now + 1.0
                if now - last_packet > 5.0:
                    raise TimeoutError("no stream packet for 5 s")
                if now >= next_report or now - start >= args.duration:
                    ovf = cam.read_reg(reg["CAPTURE_OVERFLOWS"]) - ovf0
                    bad = cam.read_reg(reg["CAPTURE_BAD_FRAMES"]) - bad0
                    print(f"{now - start:7.0f} s  frames {complete} complete, {incomplete} "
                          f"incomplete, {missing} missing, {restarts} restarts  |  "
                          f"capture overflows {ovf}, bad frames {bad}", flush=True)
                    next_report += args.interval
                    if now - start >= args.duration:
                        break
            except TimeoutError as e:
                # Link drop: wait for it to come back, then take control
                # again (the heartbeat has expired) and restart the stream.
                print(f"{now - start:7.0f} s  link lost ({e}); restarting", flush=True)
                restarts += 1
                while True:
                    time.sleep(2)
                    try:
                        cam.take_control()
                        gev.start_stream(cam, rx, local_ip, args.packet_size)
                        break
                    except (TimeoutError, gev.GevError):
                        continue
                last_packet = next_heartbeat = time.monotonic()
                block = last_block = None
                continue
            try:
                pkt = rx.sock.recv(65536)
            except socket.timeout:
                continue
            last_packet = time.monotonic()
            _, blk, info = struct.unpack(">HHI", pkt[:8])
            fmt, pid = info >> 24, info & 0xFFFFFF
            if fmt == 1:                                   # leader
                if last_block is not None:
                    gap = (blk - last_block - 1) & 0xFFFF
                    if blk < last_block:                   # wrapped past 0, which is reserved
                        gap -= 1
                    missing += gap
                last_block, block, payloads = blk, blk, 0
            elif blk != block:
                continue
            elif fmt == 3:
                payloads += 1
            elif fmt == 2:                                 # trailer
                if payloads == pid - 1:
                    complete += 1
                else:
                    incomplete += 1
                block = None
    finally:
        gev.stop_stream(cam)
        rx.close()
        cam.release()

    ok = ovf == 0
    print(f"{'PASS' if ok else 'FAIL'}: {ovf} capture overflows, {bad} bad frames, "
          f"{complete} frames complete ({complete / args.duration:.2f} fps), "
          f"{restarts} stream restarts")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())

#!/usr/bin/env python3
"""Minimal GigE Vision client: GVCP control and a GVSP frame receiver.

Used by the camd tests and as a debugging tool. It is not a replacement for
Aravis, which is the M1 acceptance client.

  python3 client/gev.py --host 127.0.0.1 discover
  python3 client/gev.py --host 127.0.0.1 xml > camera.xml
  python3 client/gev.py --host 127.0.0.1 grab --frames 3 --save frame.pgm
"""

import argparse
import socket
import struct
import sys
import time

GVCP_PORT = 3956
DISCOVERY, READREG, WRITEREG, READMEM, WRITEMEM = 0x0002, 0x0080, 0x0082, 0x0084, 0x0086

SUCCESS = 0x0000
NOT_IMPLEMENTED = 0x8001
INVALID_PARAMETER = 0x8002
INVALID_ADDRESS = 0x8003
WRITE_PROTECT = 0x8004
BAD_ALIGNMENT = 0x8005
ACCESS_DENIED = 0x8006
BUSY = 0x8007

BS_CCP, BS_HEARTBEAT, BS_FIRST_URL = 0x0A00, 0x0938, 0x0200
BS_SCP0, BS_SCPS0, BS_SCDA0 = 0x0D00, 0x0D04, 0x0D18
CCP_CONTROL = 0x2

REG = dict(WIDTH_MAX=0x10000, HEIGHT_MAX=0x10004, WIDTH=0x10008, HEIGHT=0x1000C,
           OFFSET_X=0x10010, OFFSET_Y=0x10014, PIXEL_FORMAT=0x10018,
           PAYLOAD_SIZE=0x1001C, ACQ_MODE=0x10020, ACQ_START=0x10024,
           ACQ_STOP=0x10028, FRAME_RATE=0x1002C, TEST_PATTERN=0x10030,
           TL_PARAMS_LOCKED=0x10034, FRAMES_SENT=0x10038, FRAME_RATE_MAX=0x1003C,
           CAPTURE_OVERFLOWS=0x10040, CAPTURE_BAD_FRAMES=0x10044)

MONO8, BAYER_GR8 = 0x01080001, 0x01080008


class GevError(Exception):
    def __init__(self, status, what):
        super().__init__(f"{what}: status 0x{status:04x}")
        self.status = status


def cstr(raw):
    return raw.split(b"\0", 1)[0].decode(errors="replace")


class Gvcp:
    """GVCP client bound to one local UDP port (the control channel identity)."""

    def __init__(self, host, port=GVCP_PORT, timeout=0.5):
        self.addr = (host, port)
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.sock.bind(("0.0.0.0", 0))
        self.sock.settimeout(timeout)
        self.req_id = 0

    def close(self):
        self.sock.close()

    def command(self, cmd, payload=b"", retries=3):
        """Send a command and return (status, ack payload)."""
        self.req_id = self.req_id % 0xFFFF + 1
        pkt = struct.pack(">BBHHH", 0x42, 0x01, cmd, len(payload), self.req_id) + payload
        for _ in range(retries):
            self.sock.sendto(pkt, self.addr)
            try:
                while True:
                    data, _ = self.sock.recvfrom(2048)
                    status, ack_cmd, alen, ack_id = struct.unpack(">HHHH", data[:8])
                    if ack_id == self.req_id and ack_cmd == cmd + 1:
                        return status, data[8:8 + alen]
            except socket.timeout:
                continue
        raise TimeoutError(f"no ack for command 0x{cmd:04x}")

    def discover(self):
        status, data = self.command(DISCOVERY)
        if status != SUCCESS:
            raise GevError(status, "discovery")
        version, mode, mac_hi, mac_lo = struct.unpack(">IIII", data[0:16])
        return {
            "version": f"{version >> 16}.{version & 0xFFFF}",
            "device_mode": mode,
            "mac": f"{mac_hi:04x}{mac_lo:08x}",
            "ip": socket.inet_ntoa(data[0x24:0x28]),
            "mask": socket.inet_ntoa(data[0x34:0x38]),
            "manufacturer": cstr(data[0x48:0x68]),
            "model": cstr(data[0x68:0x88]),
            "device_version": cstr(data[0x88:0xA8]),
            "info": cstr(data[0xA8:0xD8]),
            "serial": cstr(data[0xD8:0xE8]),
            "user_name": cstr(data[0xE8:0xF8]),
        }

    def read_regs(self, *addrs):
        status, data = self.command(READREG, b"".join(struct.pack(">I", a) for a in addrs))
        if status != SUCCESS:
            raise GevError(status, f"READREG {[hex(a) for a in addrs]}")
        return list(struct.unpack(f">{len(addrs)}I", data))

    def read_reg(self, addr):
        return self.read_regs(addr)[0]

    def write_reg(self, addr, value):
        status, _ = self.command(WRITEREG, struct.pack(">II", addr, value & 0xFFFFFFFF))
        if status != SUCCESS:
            raise GevError(status, f"WRITEREG 0x{addr:x}")

    def read_float(self, addr):
        return struct.unpack(">f", struct.pack(">I", self.read_reg(addr)))[0]

    def write_float(self, addr, value):
        self.write_reg(addr, struct.unpack(">I", struct.pack(">f", value))[0])

    def read_mem(self, addr, count):
        out = b""
        while len(out) < count:
            n = min(512, count - len(out))
            n4 = (n + 3) & ~3
            status, data = self.command(READMEM, struct.pack(">IHH", addr + len(out), 0, n4))
            if status != SUCCESS:
                raise GevError(status, f"READMEM 0x{addr + len(out):x}")
            out += data[4:4 + n]
        return out

    def write_mem(self, addr, data):
        status, _ = self.command(WRITEMEM, struct.pack(">I", addr) + data)
        if status != SUCCESS:
            raise GevError(status, f"WRITEMEM 0x{addr:x}")

    def take_control(self):
        self.write_reg(BS_CCP, CCP_CONTROL)

    def release(self):
        self.write_reg(BS_CCP, 0)

    def xml(self):
        """Fetch the GenICam XML named by the first URL ("Local:name;addr;len")."""
        url = cstr(self.read_mem(BS_FIRST_URL, 512))
        if not url.lower().startswith("local:"):
            raise ValueError(f"unsupported XML URL {url!r}")
        name, addr, length = url[6:].split(";")
        return name, self.read_mem(int(addr, 16), int(length, 16))


class Gvsp:
    """Receives GVSP blocks on a local UDP port."""

    def __init__(self, bind_ip="0.0.0.0", timeout=2.0):
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.sock.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 8 * 1024 * 1024)
        self.sock.bind((bind_ip, 0))
        self.sock.settimeout(timeout)
        self.port = self.sock.getsockname()[1]

    def close(self):
        self.sock.close()

    def recv_raw(self):
        return self.sock.recv(65536)

    def frames(self, count, timeout=10.0):
        """Return `count` complete frames as dicts (leader fields + data)."""
        done, cur = [], None
        deadline = time.monotonic() + timeout
        while len(done) < count:
            if time.monotonic() > deadline:
                raise TimeoutError(f"received {len(done)} of {count} frames")
            try:
                pkt = self.sock.recv(65536)
            except socket.timeout:
                continue
            status, block, info = struct.unpack(">HHI", pkt[:8])
            fmt, pid = info >> 24, info & 0xFFFFFF
            if fmt == 1:                                   # leader
                (_, ptype, ts_hi, ts_lo, pixfmt, w, h, ox, oy, _, _) = struct.unpack(
                    ">HHIIIIIIIHH", pkt[8:44])
                cur = dict(block_id=block, payload_type=ptype, timestamp=ts_hi << 32 | ts_lo,
                           pixel_format=pixfmt, width=w, height=h, offset_x=ox,
                           offset_y=oy, packets={}, status=status)
            elif cur is None or block != cur["block_id"]:
                continue                                   # joined mid-frame
            elif fmt == 3:                                 # payload
                cur["packets"][pid] = pkt[8:]
            elif fmt == 2:                                 # trailer
                ids = sorted(cur["packets"])
                cur["trailer_packet_id"] = pid
                cur["trailer_height"] = struct.unpack(">HHI", pkt[8:16])[2]
                cur["complete"] = ids == list(range(1, pid))
                cur["packet_sizes"] = [len(cur["packets"][i]) for i in ids]
                cur["data"] = b"".join(cur["packets"][i] for i in ids)
                del cur["packets"]
                done.append(cur)
                cur = None
        return done


def start_stream(gvcp, gvsp, dest_ip, packet_size=1500):
    gvcp.write_reg(BS_SCPS0, packet_size)
    gvcp.write_reg(BS_SCDA0, struct.unpack(">I", socket.inet_aton(dest_ip))[0])
    gvcp.write_reg(BS_SCP0, gvsp.port)
    gvcp.write_reg(REG["ACQ_START"], 1)


def stop_stream(gvcp):
    gvcp.write_reg(REG["ACQ_STOP"], 1)
    gvcp.write_reg(BS_SCP0, 0)


def main():
    p = argparse.ArgumentParser(description=__doc__,
                                formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--host", required=True)
    p.add_argument("--port", type=int, default=GVCP_PORT)
    sub = p.add_subparsers(dest="cmd", required=True)
    sub.add_parser("discover")
    sub.add_parser("xml")
    g = sub.add_parser("grab")
    g.add_argument("--frames", type=int, default=1)
    g.add_argument("--packet-size", type=int, default=1500)
    g.add_argument("--save", help="write the last frame as a PGM file")
    args = p.parse_args()

    gvcp = Gvcp(args.host, args.port)
    if args.cmd == "discover":
        for k, v in gvcp.discover().items():
            print(f"{k:15} {v}")
    elif args.cmd == "xml":
        _, data = gvcp.xml()
        sys.stdout.write(data.decode())
    elif args.cmd == "grab":
        gvsp = Gvsp()
        local_ip = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        local_ip.connect((args.host, args.port))
        gvcp.take_control()
        try:
            start_stream(gvcp, gvsp, local_ip.getsockname()[0], args.packet_size)
            frames = gvsp.frames(args.frames)
        finally:
            stop_stream(gvcp)
            gvcp.release()
        for f in frames:
            print(f"block {f['block_id']:5} {f['width']}x{f['height']} "
                  f"fmt 0x{f['pixel_format']:08x} ts {f['timestamp']} "
                  f"{'complete' if f['complete'] else 'INCOMPLETE'} {len(f['data'])} B")
        if args.save:
            f = frames[-1]
            with open(args.save, "wb") as out:
                out.write(b"P5\n%d %d\n255\n" % (f["width"], f["height"]) + f["data"])


if __name__ == "__main__":
    main()

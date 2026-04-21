#!/usr/bin/env python3
"""
send_led_pattern.py — PC-side UDP client for the Phase 7 LED server.

Sends LED control commands to the led_server running on the DE10-Nano.

Wire protocol (Phase 7 raw UDP baseline):
  Request  (2 bytes): [CMD] [PATTERN]
  Response (2 bytes): [STATUS] [CURRENT_PATTERN]

  CMD 0x01 = SET_PATTERN  write PATTERN to LED register
  CMD 0x02 = GET_PATTERN  read current LED register value

Usage:
  python3 send_led_pattern.py --host 192.168.1.100 --pattern 0xA5
  python3 send_led_pattern.py --host 192.168.1.100 --get
  python3 send_led_pattern.py --host 192.168.1.100 --animate chase --speed 150
  python3 send_led_pattern.py --host 192.168.1.100 --animate all
"""

import argparse
import socket
import sys
import time

# ── Protocol constants ────────────────────────────────────────────────────────
DEFAULT_PORT    = 5005
TIMEOUT_S       = 2.0

CMD_SET_PATTERN = 0x01
CMD_GET_PATTERN = 0x02

STATUS_OK              = 0x00
STATUS_ERR_UNKNOWN_CMD = 0x01
STATUS_ERR_INVALID_LEN = 0x02

STATUS_NAMES = {
    STATUS_OK:              "OK",
    STATUS_ERR_UNKNOWN_CMD: "ERR_UNKNOWN_CMD",
    STATUS_ERR_INVALID_LEN: "ERR_INVALID_LEN",
}

# ── Named animation sequences ─────────────────────────────────────────────────
ANIMATIONS = {
    "chase": [0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80],
    "breathe": [
        0x01, 0x03, 0x07, 0x0F, 0x1F, 0x3F, 0x7F, 0xFF,
        0x7F, 0x3F, 0x1F, 0x0F, 0x07, 0x03, 0x01, 0x00,
    ],
    "blink": [0xFF, 0x00],
    "stripes": [0xAA, 0x55],
    "all": [
        0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80,
        0x80, 0x40, 0x20, 0x10, 0x08, 0x04, 0x02, 0x01,
        0x01, 0x03, 0x07, 0x0F, 0x1F, 0x3F, 0x7F, 0xFF,
        0x7F, 0x3F, 0x1F, 0x0F, 0x07, 0x03, 0x01, 0x00,
        0xAA, 0x55, 0xAA, 0x55,
        0xFF, 0x00, 0xFF, 0x00,
    ],
}


def build_request(cmd: int, pattern: int = 0x00) -> bytes:
    """Encode a 2-byte request packet."""
    return bytes([cmd & 0xFF, pattern & 0xFF])


def parse_response(data: bytes) -> tuple[int, int]:
    """Decode a 2-byte response packet. Returns (status, pattern)."""
    if len(data) != 2:
        raise ValueError(f"Expected 2-byte response, got {len(data)} bytes")
    return data[0], data[1]


def send_command(sock: socket.socket, host: str, port: int,
                 cmd: int, pattern: int = 0x00) -> tuple[int, int]:
    """Send one command and return (status, current_pattern)."""
    req = build_request(cmd, pattern)
    sock.sendto(req, (host, port))
    data, _ = sock.recvfrom(256)
    return parse_response(data)


def main() -> int:
    parser = argparse.ArgumentParser(
        description="UDP client for the DE10-Nano Phase 7 LED server.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  %(prog)s --host 192.168.1.100 --pattern 0xA5
  %(prog)s --host 192.168.1.100 --get
  %(prog)s --host 192.168.1.100 --animate chase --speed 150
  %(prog)s --host 192.168.1.100 --animate all
        """,
    )
    parser.add_argument("--host",    required=True, help="Board IP address or hostname")
    parser.add_argument("--port",    type=int, default=DEFAULT_PORT,
                        help=f"UDP port (default: {DEFAULT_PORT})")
    parser.add_argument("--pattern", help="Hex LED pattern to set, e.g. 0xA5")
    parser.add_argument("--get",     action="store_true",
                        help="Read the current LED pattern")
    parser.add_argument("--animate", choices=list(ANIMATIONS),
                        help="Run a named animation (loops until Ctrl+C)")
    parser.add_argument("--speed",   type=int, default=100,
                        help="Animation step interval in ms (default: 100)")
    args = parser.parse_args()

    # Validate: exactly one action
    actions = [args.pattern is not None, args.get, args.animate is not None]
    if sum(actions) != 1:
        parser.error("Specify exactly one of --pattern, --get, or --animate.")

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.settimeout(TIMEOUT_S)

    try:
        if args.get:
            status, current = send_command(sock, args.host, args.port, CMD_GET_PATTERN)
            print(f"Current LED pattern: 0x{current:02X} "
                  f"(status: {STATUS_NAMES.get(status, f'0x{status:02X}')})")
            return 0 if status == STATUS_OK else 1

        if args.pattern is not None:
            try:
                value = int(args.pattern, 0) & 0xFF
            except ValueError:
                print(f"Error: invalid pattern '{args.pattern}' — use hex like 0xA5",
                      file=sys.stderr)
                return 1
            status, current = send_command(sock, args.host, args.port,
                                           CMD_SET_PATTERN, value)
            print(f"SET 0x{value:02X} → status={STATUS_NAMES.get(status, f'0x{status:02X}')}, "
                  f"LED=0x{current:02X}")
            return 0 if status == STATUS_OK else 1

        # Animation mode
        frames = ANIMATIONS[args.animate]
        step_s = args.speed / 1000.0
        print(f"Running animation '{args.animate}' at {args.speed} ms/step — Ctrl+C to stop")
        idx = 0
        while True:
            pattern_val = frames[idx % len(frames)]
            status, _ = send_command(sock, args.host, args.port,
                                     CMD_SET_PATTERN, pattern_val)
            if status != STATUS_OK:
                print(f"Error: server returned {STATUS_NAMES.get(status, f'0x{status:02X}')}",
                      file=sys.stderr)
                return 1
            idx += 1
            time.sleep(step_s)

    except KeyboardInterrupt:
        print("\nStopped.")
        # Turn off LEDs on exit
        try:
            send_command(sock, args.host, args.port, CMD_SET_PATTERN, 0x00)
        except Exception:
            pass
        return 0
    except socket.timeout:
        print(f"Error: no response from {args.host}:{args.port} (timeout {TIMEOUT_S}s)",
              file=sys.stderr)
        return 1
    except OSError as exc:
        print(f"Error: {exc}", file=sys.stderr)
        return 1
    finally:
        sock.close()

    return 0


if __name__ == "__main__":
    sys.exit(main())

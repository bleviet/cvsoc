#!/usr/bin/env python3
"""
gateway.py — asyncio WebSocket ↔ UDP bridge for the Phase 7.6 LED dashboard.

Architecture:
    Browser ←── WebSocket (JSON) ──→ gateway.py ←── UDP / protobuf ──→ led_server_pb (C)

The gateway:
  - Serves dashboard.html at http://localhost:8080  (minimal asyncio HTTP)
  - Accepts WebSocket connections at ws://localhost:8081
  - Translates JSON control messages → nanopb-encoded LedCommand UDP datagrams
  - Decodes LedResponse replies and broadcasts them as JSON to all browser clients
  - Polls the board every POLL_INTERVAL_S seconds to catch changes from other clients

Usage:
    python3 gateway.py --host <board-ip>
    python3 gateway.py --host 192.168.1.100 --port 5006 --http-port 8080 --ws-port 8081
    python3 gateway.py --host fe80::2833:8aff:fe95:cb3d%enx08beac224c03

Then open http://localhost:8080 in a browser.

WebSocket message format
------------------------
  Browser → gateway  (command):
    {"command": "SET_PATTERN", "pattern": 165}
    {"command": "GET_PATTERN"}

  Gateway → browser  (state broadcast):
    {"type": "state",   "pattern": 165}
    {"type": "log",     "entry": {"dir": "→", "command": "SET_PATTERN",
                                  "pattern": "0xA5", "ts": "12:34:56.789"}}
    {"type": "log",     "entry": {"dir": "←", "command": "SET_PATTERN",
                                  "status": "OK", "pattern": "0xA5", "ts": "..."}}
    {"type": "error",   "message": "...", "timestamp": "..."}
"""

import asyncio
import argparse
import json
import logging
import socket
import sys
from datetime import datetime, timezone
from pathlib import Path

try:
    from websockets import serve as ws_serve
    import websockets.exceptions
except ImportError:
    print(
        "Error: websockets package not found.\n"
        "Install with: pip install websockets",
        file=sys.stderr,
    )
    sys.exit(1)

# The generated Python protobuf stubs live in the sibling client/ directory.
sys.path.insert(0, str(Path(__file__).parent.parent / "client"))
try:
    from led_command_pb2 import LedCommand, LedResponse, CommandType, StatusCode
except ImportError:
    print(
        "Error: led_command_pb2.py not found.\n"
        "Run: make proto  (from 11_ethernet_hps_led/)",
        file=sys.stderr,
    )
    sys.exit(1)

# ── Constants ──────────────────────────────────────────────────────────────────
DEFAULT_UDP_PORT  = 5006
DEFAULT_HTTP_PORT = 8080
DEFAULT_WS_PORT   = 8081

POLL_INTERVAL_S = 2.0   # board state poll period
UDP_TIMEOUT_S   = 2.0   # recvfrom timeout
LOG_MAX         = 50    # max packet-log entries kept in the browser

STATUS_NAMES = {
    StatusCode.OK:               "OK",
    StatusCode.ERR_UNKNOWN_CMD:  "ERR_UNKNOWN_CMD",
    StatusCode.ERR_DECODE_FAIL:  "ERR_DECODE_FAIL",
    StatusCode.ERR_INVALID_DATA: "ERR_INVALID_DATA",
}

# ── Logging ────────────────────────────────────────────────────────────────────
logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s  %(levelname)-7s  %(message)s",
    datefmt="%H:%M:%S",
)
log = logging.getLogger("gateway")

# ── Shared state ──────────────────────────────────────────────────────────────
_clients: set = set()
_last_pattern: int = 0


# ── Protobuf helpers ───────────────────────────────────────────────────────────

def pb_encode(command: int, pattern: int = 0) -> bytes:
    """Encode a LedCommand as a nanopb-compatible serialised byte string."""
    msg = LedCommand()
    msg.command = command
    msg.pattern = pattern & 0xFF
    return msg.SerializeToString()


def pb_decode(data: bytes) -> dict:
    """Decode a serialised LedResponse into a plain dict."""
    resp = LedResponse()
    resp.ParseFromString(data)
    return {
        "status":  STATUS_NAMES.get(resp.status, f"ERR_0x{resp.status:02X}"),
        "pattern": resp.pattern,
    }


# ── UDP client ────────────────────────────────────────────────────────────────

class UDPClient:
    """
    Blocking UDP client that communicates with led_server_pb on the board.

    All network I/O is blocking and must be called via loop.run_in_executor()
    so as not to stall the asyncio event loop.
    """

    def __init__(self, host: str, port: int) -> None:
        info = socket.getaddrinfo(host, port, type=socket.SOCK_DGRAM)
        self._af, _, _, _, self._dest = info[0]
        self._sock = socket.socket(self._af, socket.SOCK_DGRAM)
        self._sock.settimeout(UDP_TIMEOUT_S)
        log.info(
            "UDP → %s  port %d  (%s)",
            host, port, "IPv6" if self._af == socket.AF_INET6 else "IPv4",
        )

    def send(self, command: int, pattern: int = 0) -> dict:
        """Send one LedCommand and return the decoded LedResponse as a dict."""
        self._sock.sendto(pb_encode(command, pattern), self._dest)
        raw, _ = self._sock.recvfrom(256)
        return pb_decode(raw)

    def close(self) -> None:
        self._sock.close()


# ── Helpers ────────────────────────────────────────────────────────────────────

def _ts() -> str:
    """Return the current UTC time as HH:MM:SS.mmm."""
    return datetime.now(timezone.utc).strftime("%H:%M:%S.%f")[:-3]


async def _broadcast(msg: dict) -> None:
    """Send a JSON message to every connected WebSocket client."""
    if not _clients:
        return
    text = json.dumps(msg)
    await asyncio.gather(
        *(c.send(text) for c in list(_clients)),
        return_exceptions=True,
    )


# ── WebSocket handler ─────────────────────────────────────────────────────────

async def _ws_connection(websocket, udp: UDPClient) -> None:
    """Manage one WebSocket connection for its lifetime."""
    global _last_pattern
    _clients.add(websocket)
    log.info("WS connect    [%d client(s)]", len(_clients))
    try:
        # Push the current LED state to the newly connected browser.
        await websocket.send(
            json.dumps({"type": "state", "pattern": _last_pattern})
        )
        async for raw in websocket:
            await _handle_message(raw, udp)
    except websockets.exceptions.ConnectionClosed:
        pass
    finally:
        _clients.discard(websocket)
        log.info("WS disconnect  [%d client(s)]", len(_clients))


async def _handle_message(raw: str, udp: UDPClient) -> None:
    """Translate one browser JSON command into a UDP round-trip and broadcast results."""
    global _last_pattern
    try:
        msg = json.loads(raw)
    except json.JSONDecodeError:
        await _broadcast({"type": "error", "message": "invalid JSON", "timestamp": _ts()})
        return

    cmd_name = msg.get("command", "SET_PATTERN")
    pattern  = int(msg.get("pattern", 0)) & 0xFF
    command  = (
        CommandType.GET_PATTERN
        if cmd_name == "GET_PATTERN"
        else CommandType.SET_PATTERN
    )

    pat_str = f"0x{pattern:02X}" if command == CommandType.SET_PATTERN else "—"
    await _broadcast({"type": "log", "entry": {
        "dir": "→", "command": cmd_name, "pattern": pat_str, "ts": _ts(),
    }})
    log.info("→  %-14s  pattern=%s", cmd_name, pat_str)

    loop = asyncio.get_event_loop()
    try:
        result = await loop.run_in_executor(None, udp.send, command, pattern)
    except socket.timeout:
        await _broadcast({
            "type": "error",
            "message": "UDP timeout — is the board reachable and led_server_pb running?",
            "timestamp": _ts(),
        })
        log.warning("UDP timeout")
        return
    except Exception as exc:
        await _broadcast({"type": "error", "message": str(exc), "timestamp": _ts()})
        log.warning("UDP error: %s", exc)
        return

    _last_pattern = result["pattern"]
    await _broadcast({"type": "log", "entry": {
        "dir": "←", "command": cmd_name,
        "status": result["status"],
        "pattern": f"0x{result['pattern']:02X}",
        "ts": _ts(),
    }})
    await _broadcast({"type": "state", "pattern": result["pattern"]})
    log.info(
        "←  %-14s  status=%-16s  pattern=0x%02X",
        cmd_name, result["status"], result["pattern"],
    )


# ── Board polling ─────────────────────────────────────────────────────────────

async def _poll_board(udp: UDPClient) -> None:
    """
    Periodically read the board's current LED state and push updates to
    all connected browser clients.  This keeps the dashboard in sync even
    when another client (e.g. the CLI Python script) changes the pattern.
    """
    global _last_pattern
    loop = asyncio.get_event_loop()
    while True:
        await asyncio.sleep(POLL_INTERVAL_S)
        if not _clients:
            continue
        try:
            result = await loop.run_in_executor(
                None, udp.send, CommandType.GET_PATTERN, 0
            )
            if result["pattern"] != _last_pattern:
                _last_pattern = result["pattern"]
                await _broadcast({"type": "state", "pattern": _last_pattern})
                log.info("Poll: pattern changed → 0x%02X", _last_pattern)
        except Exception:
            pass  # Board unreachable during poll — silently skip


# ── Minimal asyncio HTTP server ───────────────────────────────────────────────

_HTML_FILE = Path(__file__).parent / "dashboard.html"
_ws_port_global: int = DEFAULT_WS_PORT  # set by _run() before starting


def _make_http_handler():
    """Return an asyncio stream handler that serves dashboard.html."""

    async def _handler(reader: asyncio.StreamReader, writer: asyncio.StreamWriter) -> None:
        try:
            request = await reader.read(4096)
            # Serve 404 for anything that isn't the dashboard (e.g. favicon)
            if b"favicon" in request:
                writer.write(b"HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n\r\n")
                await writer.drain()
                return
            body = _HTML_FILE.read_bytes()
            # Inject the WebSocket port so dashboard.html can connect to the
            # right port without hard-coding it.
            body = body.replace(b"{{WS_PORT}}", str(_ws_port_global).encode())
            writer.write(
                b"HTTP/1.1 200 OK\r\n"
                b"Content-Type: text/html; charset=utf-8\r\n"
                b"Connection: close\r\n"
                b"\r\n"
                + body
            )
            await writer.drain()
        except Exception:
            pass
        finally:
            writer.close()

    return _handler


# ── Entry point ───────────────────────────────────────────────────────────────

async def _run(
    board_host: str,
    udp_port: int,
    http_port: int,
    ws_port: int,
) -> None:
    global _ws_port_global
    _ws_port_global = ws_port

    udp = UDPClient(board_host, udp_port)
    try:
        http_server = await asyncio.start_server(
            _make_http_handler(), "localhost", http_port
        )
        async with ws_serve(
            lambda ws: _ws_connection(ws, udp), "localhost", ws_port
        ):
            log.info("Dashboard  →  http://localhost:%d", http_port)
            log.info("WebSocket  →  ws://localhost:%d  (internal)", ws_port)
            log.info("Press Ctrl+C to stop.")
            async with http_server:
                await _poll_board(udp)
    finally:
        udp.close()


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Phase 7.6 gateway — WebSocket ↔ UDP bridge for the LED dashboard.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  gateway.py --host 192.168.1.100
  gateway.py --host fe80::2833:8aff:fe95:cb3d%enx08beac224c03
  gateway.py --host <board-ip> --port 5006 --http-port 8080 --ws-port 8081
        """,
    )
    parser.add_argument(
        "--host", required=True,
        help="Board IP address (IPv4 or IPv6, with optional zone ID for link-local)",
    )
    parser.add_argument(
        "--port", type=int, default=DEFAULT_UDP_PORT,
        help=f"UDP port of led_server_pb on the board (default: {DEFAULT_UDP_PORT})",
    )
    parser.add_argument(
        "--http-port", type=int, default=DEFAULT_HTTP_PORT,
        help=f"HTTP port for serving dashboard.html (default: {DEFAULT_HTTP_PORT})",
    )
    parser.add_argument(
        "--ws-port", type=int, default=DEFAULT_WS_PORT,
        help=f"WebSocket port for browser connections (default: {DEFAULT_WS_PORT})",
    )
    args = parser.parse_args()

    try:
        asyncio.run(_run(args.host, args.port, args.http_port, args.ws_port))
    except KeyboardInterrupt:
        log.info("Gateway stopped.")


if __name__ == "__main__":
    main()

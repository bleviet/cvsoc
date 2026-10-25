#!/usr/bin/env python3
"""
opcua_client.py — Pure-Python OPC UA Binary (opc.tcp) & Part 14 UADP PubSub
                  CLI client for the DE10-Nano (Phase 11 — 17_opcua_hps_led).

Zero external dependencies required (uses only the Python standard library).
Supports IPv4, global IPv6, and link-local IPv6 with Zone ID (fe80::...%iface).

Examples:
  # 1. Dynamically discover the DE10-Nano Address Space (no .proto schema needed!):
  python3 client/opcua_client.py --host 192.168.1.100 browse

  # 2. Read the current 8-bit LED pattern or FPGA Manager state:
  python3 client/opcua_client.py --host 192.168.1.100 read --node fpga.led_pattern
  python3 client/opcua_client.py --host 192.168.1.100 read --node fpga.bitstream_state

  # 3. Write an 8-bit LED pattern or toggle a single LED bit:
  python3 client/opcua_client.py --host 192.168.1.100 write --node fpga.led_pattern --value 0xA5
  python3 client/opcua_client.py --host 192.168.1.100 write --node fpga.led0 --value true

  # 4. Invoke an OPC UA Method (RPC) on the board:
  python3 client/opcua_client.py --host 192.168.1.100 call --method fpga.set_pattern --arg 0x5A
  python3 client/opcua_client.py --host 192.168.1.100 call --method fpga.invert_pattern

  # 5. Subscribe to Report-by-Exception DataChangeNotifications:
  python3 client/opcua_client.py --host 192.168.1.100 subscribe --count 5

  # 6. Listen for OPC UA Part 14 UADP Binary PubSub telemetry over UDP:
  python3 client/opcua_client.py pubsub-listen --port 4841 --count 3
"""

import argparse
import socket
import struct
import sys
import time
from dataclasses import dataclass
from typing import Any, Dict, List, Optional, Tuple

# ── Standard OPC UA Constants (IEC 62541-6) ───────────────────────────────────
UA_DEFAULT_TCP_PORT    = 4840
UA_DEFAULT_PUBSUB_PORT = 4841

UA_STATUSCODE_GOOD                  = 0x00000000
UA_STATUSCODE_BADINTERNALERROR      = 0x80020000
UA_STATUSCODE_BADDECODINGERROR      = 0x80070000
UA_STATUSCODE_BADSERVICEUNSUPPORTED = 0x800B0000
UA_STATUSCODE_BADNODEIDUNKNOWN      = 0x80340000
UA_STATUSCODE_BADNOTREADABLE        = 0x803A0000
UA_STATUSCODE_BADNOTWRITABLE        = 0x803B0000
UA_STATUSCODE_BADOUTOFRANGE         = 0x803C0000
UA_STATUSCODE_BADTYPEMISMATCH       = 0x80740000
UA_STATUSCODE_BADMETHODINVALID      = 0x80750000

STATUS_NAMES: Dict[int, str] = {
    UA_STATUSCODE_GOOD:                  "Good (0x00000000)",
    UA_STATUSCODE_BADINTERNALERROR:      "BadInternalError (0x80020000)",
    UA_STATUSCODE_BADDECODINGERROR:      "BadDecodingError (0x80070000)",
    UA_STATUSCODE_BADSERVICEUNSUPPORTED: "BadServiceUnsupported (0x800B0000)",
    UA_STATUSCODE_BADNODEIDUNKNOWN:      "BadNodeIdUnknown (0x80340000)",
    UA_STATUSCODE_BADNOTREADABLE:        "BadNotReadable (0x803A0000)",
    UA_STATUSCODE_BADNOTWRITABLE:        "BadNotWritable (0x803B0000)",
    UA_STATUSCODE_BADOUTOFRANGE:         "BadOutOfRange (0x803C0000)",
    UA_STATUSCODE_BADTYPEMISMATCH:       "BadTypeMismatch (0x80740000)",
    UA_STATUSCODE_BADMETHODINVALID:      "BadMethodInvalid (0x80750000)",
}

UA_TYPE_BOOLEAN = 1
UA_TYPE_BYTE    = 3
UA_TYPE_UINT32  = 7
UA_TYPE_STRING  = 12

TYPE_NAMES: Dict[int, str] = {
    0:               "—",
    UA_TYPE_BOOLEAN: "Boolean",
    UA_TYPE_BYTE:    "Byte",
    UA_TYPE_UINT32:  "UInt32",
    UA_TYPE_STRING:  "String",
}

UA_NODECLASS_OBJECT   = 1
UA_NODECLASS_VARIABLE = 2
UA_NODECLASS_METHOD   = 4

NODECLASS_NAMES: Dict[int, str] = {
    UA_NODECLASS_OBJECT:   "Object",
    UA_NODECLASS_VARIABLE: "Variable",
    UA_NODECLASS_METHOD:   "Method",
}

UA_SVC_BROWSE_REQ     = 527
UA_SVC_BROWSE_RESP    = 530
UA_SVC_READ_REQ       = 631
UA_SVC_READ_RESP      = 634
UA_SVC_WRITE_REQ      = 673
UA_SVC_WRITE_RESP     = 676
UA_SVC_CALL_REQ       = 712
UA_SVC_CALL_RESP      = 715
UA_SVC_CREATESUB_REQ  = 787
UA_SVC_CREATESUB_RESP = 790
UA_SVC_PUBLISH_RESP   = 829


# ── Little-Endian Binary Encoding / Decoding Helpers ──────────────────────────

def encode_ua_string(s: Optional[str]) -> bytes:
    if s is None:
        return struct.pack("<I", 0xFFFFFFFF)
    raw = s.encode("utf-8")
    return struct.pack("<I", len(raw)) + raw


def decode_ua_string(buf: bytes, pos: int) -> Tuple[str, int]:
    (length,) = struct.unpack_from("<I", buf, pos)
    pos += 4
    if length == 0xFFFFFFFF:
        return "", pos
    s = buf[pos : pos + length].decode("utf-8", errors="replace")
    return s, pos + length


def normalize_node_id(node_str: str) -> Tuple[int, str]:
    """Accepts either 'ns=1;s=fpga.led_pattern' or shorthand 'fpga.led_pattern'."""
    s = node_str.strip()
    if s.startswith("ns="):
        parts = s.split(";", 1)
        ns = int(parts[0].split("=", 1)[1])
        rest = parts[1]
        if rest.startswith("s="):
            rest = rest[2:]
        return ns, rest
    return 1, s


def encode_nodeid_string(ns: int, identifier: str) -> bytes:
    return struct.pack("<BH", 0x03, ns) + encode_ua_string(identifier)


def decode_nodeid_string(buf: bytes, pos: int) -> Tuple[Tuple[int, str], int]:
    (enc,) = struct.unpack_from("<B", buf, pos)
    pos += 1
    if enc == 0x03:
        (ns,) = struct.unpack_from("<H", buf, pos)
        pos += 2
        ident, pos = decode_ua_string(buf, pos)
        return (ns, ident), pos
    elif enc == 0x01:
        ns, num = struct.unpack_from("<BH", buf, pos)
        return (ns, f"i={num}"), pos + 3
    raise ValueError(f"Unsupported NodeId encoding 0x{enc:02X}")


def encode_variant(val_type: int, val: Any) -> bytes:
    if val_type == UA_TYPE_BOOLEAN:
        return struct.pack("<BB", UA_TYPE_BOOLEAN, 1 if val else 0)
    elif val_type == UA_TYPE_BYTE:
        return struct.pack("<BB", UA_TYPE_BYTE, int(val) & 0xFF)
    elif val_type == UA_TYPE_UINT32:
        return struct.pack("<BI", UA_TYPE_UINT32, int(val) & 0xFFFFFFFF)
    elif val_type == UA_TYPE_STRING:
        return struct.pack("<B", UA_TYPE_STRING) + encode_ua_string(str(val))
    raise ValueError(f"Unsupported UA_BuiltinType: {val_type}")


def decode_variant(buf: bytes, pos: int) -> Tuple[Tuple[int, Any], int]:
    (vtype,) = struct.unpack_from("<B", buf, pos)
    pos += 1
    if vtype == UA_TYPE_BOOLEAN:
        (b,) = struct.unpack_from("<B", buf, pos)
        return (vtype, bool(b)), pos + 1
    elif vtype == UA_TYPE_BYTE:
        (u8,) = struct.unpack_from("<B", buf, pos)
        return (vtype, u8), pos + 1
    elif vtype == UA_TYPE_UINT32:
        (u32,) = struct.unpack_from("<I", buf, pos)
        return (vtype, u32), pos + 4
    elif vtype == UA_TYPE_STRING:
        s, pos = decode_ua_string(buf, pos)
        return (vtype, s), pos
    raise ValueError(f"Unknown Variant type id {vtype}")


def build_chunk(chunk_type: bytes, payload: bytes) -> bytes:
    total_len = 8 + len(payload)
    return chunk_type + struct.pack("<I", total_len) + payload


def build_hel_chunk(endpoint_url: str) -> bytes:
    body = struct.pack("<IIIII", 0, 65536, 65536, 65536, 1) + encode_ua_string(endpoint_url)
    return build_chunk(b"HELF", body)


def build_opn_chunk(secure_channel_id: int = 0) -> bytes:
    body = struct.pack("<I", secure_channel_id) + encode_ua_string("http://opcfoundation.org/UA/SecurityPolicy#None")
    return build_chunk(b"OPNF", body)


def build_msg_chunk(service_id: int, service_payload: bytes,
                    secure_channel_id: int = 1, token_id: int = 1,
                    seq_num: int = 1, req_id: int = 1) -> bytes:
    sec_hdr = struct.pack("<IIII", secure_channel_id, token_id, seq_num, req_id)
    svc_node = struct.pack("<BBH", 0x01, 0x00, service_id)
    return build_chunk(b"MSGF", sec_hdr + svc_node + service_payload)


@dataclass
class BrowseEntry:
    namespace: int
    identifier: str
    parent_id: str
    browse_name: str
    display_name: str
    description: str
    node_class: int
    data_type: int
    access_level: int
    value: Any

    @property
    def node_id_str(self) -> str:
        return f"ns={self.namespace};s={self.identifier}"

    @property
    def access_str(self) -> str:
        if self.node_class == UA_NODECLASS_METHOD:
            return "Call"
        if self.node_class == UA_NODECLASS_OBJECT:
            return "—"
        r = "R" if (self.access_level & 0x01) else ""
        w = "W" if (self.access_level & 0x02) else ""
        return f"{r}/{w}" if (r and w) else (r or w or "—")


def decode_uadp_frame(raw: bytes) -> Dict[str, Any]:
    """Decodes an OPC UA Part 14 UADP Binary NetworkMessage (24 bytes)."""
    if len(raw) < 24:
        raise ValueError(f"UADP frame too short ({len(raw)} bytes)")
    ver_flags, ext_flags, pub_id, count, writer_id, ds_flags, seq_num, field_count = (
        struct.unpack_from("<BBHBHBHH", raw, 0)
    )
    pos = 12
    (_, led_pattern), pos = decode_variant(raw, pos)
    (_, op_count), pos    = decode_variant(raw, pos)
    (_, uptime_sec), pos  = decode_variant(raw, pos)
    return {
        "uadp_version": ver_flags & 0x0F,
        "publisher_id": pub_id,
        "dataset_writer_id": writer_id,
        "sequence_number": seq_num,
        "field_count": field_count,
        "led_pattern": led_pattern,
        "op_count": op_count,
        "uptime_sec": uptime_sec,
        "wire_bytes": len(raw),
    }


# ── Dual-Stack OPC UA TCP Client ──────────────────────────────────────────────

class OpcUaClient:
    def __init__(self, host: str, port: int = UA_DEFAULT_TCP_PORT,
                 timeout: float = 3.0, dump_hex: bool = False):
        self.host = host
        self.port = port
        self.timeout = timeout
        self.dump_hex = dump_hex
        self.sock: Optional[socket.socket] = None
        self.secure_channel_id = 1
        self.token_id = 1
        self.seq_num = 0
        self.req_id = 0

    def __enter__(self) -> "OpcUaClient":
        self.connect()
        return self

    def __exit__(self, exc_type, exc_val, exc_tb) -> None:
        self.close()

    def _log_hex(self, direction: str, data: bytes) -> None:
        if self.dump_hex:
            hex_str = " ".join(f"{b:02x}" for b in data)
            ascii_hdr = data[:4].decode("ascii", errors="replace")
            print(f"  [{direction}] {ascii_hdr} ({len(data):3d} B): {hex_str}")

    def _recv_exact(self, n: int) -> bytes:
        assert self.sock is not None
        chunks = []
        got = 0
        while got < n:
            part = self.sock.recv(n - got)
            if not part:
                raise ConnectionError("Server closed OPC UA TCP connection")
            chunks.append(part)
            got += len(part)
        return b"".join(chunks)

    def _recv_chunk(self) -> bytes:
        hdr = self._recv_exact(8)
        (_, total_size) = struct.unpack("<4sI", hdr)
        if total_size < 8 or total_size > 65536:
            raise ValueError(f"Invalid OPC UA chunk size: {total_size}")
        body = self._recv_exact(total_size - 8) if total_size > 8 else b""
        full = hdr + body
        self._log_hex("RX ←", full)
        return full

    def _send_chunk(self, chunk: bytes) -> None:
        assert self.sock is not None
        self._log_hex("TX →", chunk)
        self.sock.sendall(chunk)

    def connect(self) -> None:
        infos = socket.getaddrinfo(self.host, self.port, socket.AF_UNSPEC, socket.SOCK_STREAM)
        if not infos:
            raise ConnectionError(f"Cannot resolve host: {self.host}")
        af, socktype, proto, _, sa = infos[0]
        self.sock = socket.socket(af, socktype, proto)
        self.sock.settimeout(self.timeout)
        try:
            self.sock.connect(sa)

            # Step 1: HEL -> ACK
            endpoint_url = f"opc.tcp://{self.host}:{self.port}"
            self._send_chunk(build_hel_chunk(endpoint_url))
            ack = self._recv_chunk()
            if ack[:4] != b"ACKF":
                raise ConnectionError(f"Expected ACKF from server, got {ack[:4]!r}")

            # Step 2: OPN -> OPN (OpenSecureChannel)
            self._send_chunk(build_opn_chunk(0))
            opn = self._recv_chunk()
            if opn[:4] != b"OPNF":
                raise ConnectionError(f"Expected OPNF from server, got {opn[:4]!r}")
            self.secure_channel_id, self.token_id = struct.unpack_from("<II", opn, 8)
        except Exception:
            self.sock.close()
            self.sock = None
            raise

    def close(self) -> None:
        if self.sock is not None:
            try:
                self._send_chunk(build_chunk(b"CLOF", struct.pack("<II", self.secure_channel_id, self.token_id)))
            except OSError:
                pass
            try:
                self.sock.close()
            except OSError:
                pass
            self.sock = None

    def _send_service_request(self, service_id: int, payload: bytes) -> Tuple[int, int, bytes, int]:
        self.seq_num += 1
        self.req_id += 1
        req = build_msg_chunk(
            service_id, payload,
            self.secure_channel_id, self.token_id, self.seq_num, self.req_id
        )
        self._send_chunk(req)
        resp = self._recv_chunk()
        if resp[:4] != b"MSGF" or len(resp) < 32:
            raise ValueError(f"Invalid MSGF response ({len(resp)} bytes)")
        (_, _, resp_svc_id, status_code) = struct.unpack_from("<BBHI", resp, 24)
        return resp_svc_id, status_code, resp, 32

    def browse(self, parent_filter: str = "") -> List[BrowseEntry]:
        payload = encode_ua_string(parent_filter)
        _, status, resp, pos = self._send_service_request(UA_SVC_BROWSE_REQ, payload)
        if status != UA_STATUSCODE_GOOD:
            raise RuntimeError(f"Browse failed: {STATUS_NAMES.get(status, hex(status))}")
        (count,) = struct.unpack_from("<I", resp, pos)
        pos += 4
        entries: List[BrowseEntry] = []
        for _ in range(count):
            (ns, ident), pos = decode_nodeid_string(resp, pos)
            parent_id, pos   = decode_ua_string(resp, pos)
            bname, pos       = decode_ua_string(resp, pos)
            dname, pos       = decode_ua_string(resp, pos)
            desc, pos        = decode_ua_string(resp, pos)
            nclass, dtype, access = struct.unpack_from("<BBB", resp, pos)
            pos += 3
            (_, val), pos = decode_variant(resp, pos)
            entries.append(BrowseEntry(
                namespace=ns,
                identifier=ident,
                parent_id=parent_id,
                browse_name=bname,
                display_name=dname,
                description=desc,
                node_class=nclass,
                data_type=dtype,
                access_level=access,
                value=val,
            ))
        return entries

    def read(self, node_id: str) -> Tuple[int, Optional[int], Any]:
        ns, ident = normalize_node_id(node_id)
        payload = encode_nodeid_string(ns, ident)
        _, status, resp, pos = self._send_service_request(UA_SVC_READ_REQ, payload)
        if status != UA_STATUSCODE_GOOD:
            return status, None, None
        (vtype, val), _ = decode_variant(resp, pos)
        return status, vtype, val

    def write(self, node_id: str, value: Any, value_type: Optional[int] = None) -> int:
        ns, ident = normalize_node_id(node_id)
        if value_type is None:
            if ident.startswith("fpga.led") and ident != "fpga.led_pattern":
                value_type = UA_TYPE_BOOLEAN
            else:
                value_type = UA_TYPE_BYTE
        payload = encode_nodeid_string(ns, ident) + encode_variant(value_type, value)
        _, status, _, _ = self._send_service_request(UA_SVC_WRITE_REQ, payload)
        return status

    def call(self, method_id: str, arg_value: Optional[Any] = None,
             arg_type: int = UA_TYPE_UINT32) -> Tuple[int, Any]:
        ns, ident = normalize_node_id(method_id)
        payload = encode_nodeid_string(ns, ident)
        if arg_value is not None:
            payload += encode_variant(arg_type, arg_value)
        _, status, resp, pos = self._send_service_request(UA_SVC_CALL_REQ, payload)
        if status != UA_STATUSCODE_GOOD:
            return status, None
        (_, out_val), _ = decode_variant(resp, pos)
        return status, out_val

    def create_subscription(self, node_id: str = "fpga.led_pattern") -> Tuple[int, int]:
        ns, ident = normalize_node_id(node_id)
        payload = encode_nodeid_string(ns, ident)
        _, status, resp, pos = self._send_service_request(UA_SVC_CREATESUB_REQ, payload)
        if status != UA_STATUSCODE_GOOD:
            return status, 0
        (sub_id,) = struct.unpack_from("<I", resp, pos)
        return status, sub_id

    def wait_for_notification(self) -> Tuple[str, Any]:
        resp = self._recv_chunk()
        if resp[:4] != b"MSGF" or len(resp) < 32:
            raise ValueError("Invalid notification chunk")
        (_, _, svc_id, status) = struct.unpack_from("<BBHI", resp, 24)
        if svc_id != UA_SVC_PUBLISH_RESP or status != UA_STATUSCODE_GOOD:
            raise RuntimeError(f"Unexpected notification svc={svc_id} status=0x{status:08X}")
        (ns, ident), pos = decode_nodeid_string(resp, 32)
        (_, val), _ = decode_variant(resp, pos)
        return f"ns={ns};s={ident}", val


# ── CLI Formatting & Subcommands ──────────────────────────────────────────────

def format_value(dtype: int, val: Any, nclass: int = UA_NODECLASS_VARIABLE) -> str:
    if nclass != UA_NODECLASS_VARIABLE:
        return "—"
    if dtype == UA_TYPE_BYTE and isinstance(val, int):
        bits = "".join("●" if (val & (1 << i)) else "○" for i in range(7, -1, -1))
        return f"0x{val:02X} ({bits})"
    if dtype == UA_TYPE_BOOLEAN:
        return "true (ON)" if val else "false (OFF)"
    return str(val)


def parse_cli_value(raw: str) -> Any:
    low = raw.strip().lower()
    if low in ("true", "on", "yes"):
        return True
    if low in ("false", "off", "no"):
        return False
    return int(raw, 0)


def main() -> int:
    parser = argparse.ArgumentParser(
        description="OPC UA Binary (opc.tcp + Part 14 UADP) CLI Client for DE10-Nano"
    )
    parser.add_argument("--host", default="127.0.0.1",
                        help="Board IPv4 or IPv6 address (default: 127.0.0.1)")
    parser.add_argument("--port", type=int, default=UA_DEFAULT_TCP_PORT,
                        help=f"OPC UA TCP port (default: {UA_DEFAULT_TCP_PORT})")
    parser.add_argument("--dump-hex", action="store_true",
                        help="Print raw HELF/ACKF/OPNF/MSGF wire bytes")

    sub = parser.add_subparsers(dest="cmd", required=True)

    p_browse = sub.add_parser("browse", help="Discover nodes in the OPC UA Address Space")
    p_browse.add_argument("--parent", default="", help="Optional parent object filter (e.g. fpga)")

    p_read = sub.add_parser("read", help="Read an OPC UA Variable node")
    p_read.add_argument("--node", default="fpga.led_pattern",
                        help="NodeId string (e.g. fpga.led_pattern, fpga.led0, fpga.bitstream_state)")

    p_write = sub.add_parser("write", help="Write an OPC UA Variable node")
    p_write.add_argument("--node", default="fpga.led_pattern",
                         help="NodeId string (e.g. fpga.led_pattern or fpga.led0..7)")
    p_write.add_argument("--value", required=True,
                         help="Value to write (e.g. 0xA5, 255, true, false)")

    p_call = sub.add_parser("call", help="Invoke an OPC UA Method node on the server")
    p_call.add_argument("--method", default="fpga.invert_pattern",
                        help="Method NodeId (fpga.set_pattern or fpga.invert_pattern)")
    p_call.add_argument("--arg", default=None,
                        help="Optional integer argument for fpga.set_pattern (e.g. 0x5A)")

    p_sub = sub.add_parser("subscribe", help="Subscribe to DataChangeNotifications on a node")
    p_sub.add_argument("--node", default="fpga.led_pattern", help="NodeId to monitor")
    p_sub.add_argument("--count", type=int, default=3,
                       help="Number of notifications to receive before exiting")
    p_sub.add_argument("--timeout", type=float, default=15.0,
                       help="Socket timeout in seconds")

    p_ps = sub.add_parser("pubsub-listen", help="Listen for UDP Part 14 UADP telemetry frames")
    p_ps.add_argument("--udp-port", type=int, default=UA_DEFAULT_PUBSUB_PORT,
                      help=f"UDP port to bind (default: {UA_DEFAULT_PUBSUB_PORT})")
    p_ps.add_argument("--count", type=int, default=3,
                      help="Number of UADP frames to decode before exiting")

    p_demo = sub.add_parser("demo", help="Run a live LED animation over OPC UA")
    p_demo.add_argument("--mode", choices=["knight", "counter"], default="knight")
    p_demo.add_argument("--steps", type=int, default=16)

    args = parser.parse_args()

    if args.cmd == "pubsub-listen":
        sock = socket.socket(socket.AF_INET6, socket.SOCK_DGRAM)
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        sock.setsockopt(socket.IPPROTO_IPV6, socket.IPV6_V6ONLY, 0)
        sock.bind(("::", args.udp_port))
        if args.host not in ("127.0.0.1", "::1", "localhost"):
            with OpcUaClient(args.host, args.port, timeout=3.0) as c:
                c.read("fpga.led_pattern")
        print(f"Listening for OPC UA Part 14 UADP frames on UDP port {args.udp_port}...")
        for i in range(args.count):
            raw, addr = sock.recvfrom(256)
            decoded = decode_uadp_frame(raw)
            print(
                f"  [{i+1}/{args.count}] From {addr[0]}:{addr[1]} ({decoded['wire_bytes']} B) | "
                f"PubId=0x{decoded['publisher_id']:04X} WriterId={decoded['dataset_writer_id']} "
                f"Seq={decoded['sequence_number']} | "
                f"LED=0x{decoded['led_pattern']:02X} OpCount={decoded['op_count']} "
                f"Uptime={decoded['uptime_sec']}s"
            )
        sock.close()
        return 0

    timeout = getattr(args, "timeout", 5.0)
    with OpcUaClient(args.host, args.port, timeout=timeout, dump_hex=args.dump_hex) as client:
        if args.cmd == "browse":
            entries = client.browse(args.parent)
            print(f"\nOPC UA Address Space on opc.tcp://{args.host}:{args.port} ({len(entries)} nodes):")
            print("-" * 100)
            print(f"{'NodeId':<28} {'Class':<10} {'BrowseName':<17} {'Type':<9} {'Access':<7} {'Current Value'}")
            print("-" * 100)
            for e in entries:
                val_str = format_value(e.data_type, e.value, e.node_class)
                print(
                    f"{e.node_id_str:<28} "
                    f"{NODECLASS_NAMES.get(e.node_class, '?'):<10} "
                    f"{e.browse_name:<17} "
                    f"{TYPE_NAMES.get(e.data_type, '—'):<9} "
                    f"{e.access_str:<7} "
                    f"{val_str}"
                )
            print("-" * 100)

        elif args.cmd == "read":
            status, vtype, val = client.read(args.node)
            if status != UA_STATUSCODE_GOOD:
                print(f"Read failed: {STATUS_NAMES.get(status, hex(status))}", file=sys.stderr)
                return 1
            print(f"{args.node} ({TYPE_NAMES.get(vtype or 0, '?')}) = {format_value(vtype or 0, val)}")

        elif args.cmd == "write":
            val = parse_cli_value(args.value)
            status = client.write(args.node, val)
            if status != UA_STATUSCODE_GOOD:
                print(f"Write failed: {STATUS_NAMES.get(status, hex(status))}", file=sys.stderr)
                return 1
            _, vtype, readback = client.read("fpga.led_pattern")
            print(f"Write {args.node} <- {args.value}: {STATUS_NAMES[status]} | LED Register = {format_value(vtype or 3, readback)}")

        elif args.cmd == "call":
            arg_val = parse_cli_value(args.arg) if args.arg is not None else None
            status, out_val = client.call(args.method, arg_val, UA_TYPE_UINT32)
            if status != UA_STATUSCODE_GOOD:
                print(f"Call {args.method} failed: {STATUS_NAMES.get(status, hex(status))}", file=sys.stderr)
                return 1
            print(f"Call {args.method}({args.arg or ''}) -> {STATUS_NAMES[status]} | Output LED = 0x{out_val:02X}")

        elif args.cmd == "subscribe":
            status, sub_id = client.create_subscription(args.node)
            if status != UA_STATUSCODE_GOOD:
                print(f"CreateSubscription failed: {STATUS_NAMES.get(status, hex(status))}", file=sys.stderr)
                return 1
            print(f"Created SubscriptionId={sub_id} on '{args.node}'. Waiting for {args.count} notifications...")
            for i in range(args.count):
                nid, val = client.wait_for_notification()
                print(f"  [Notification {i+1}/{args.count}] {nid} -> 0x{val:02X}")

        elif args.cmd == "demo":
            print(f"Running OPC UA '{args.mode}' animation ({args.steps} steps)...")
            for step in range(args.steps):
                if args.mode == "knight":
                    pos = step % 14
                    bit = pos if pos < 8 else 14 - pos
                    pat = 1 << bit
                else:
                    pat = step & 0xFF
                client.write("fpga.led_pattern", pat)
                time.sleep(0.08)
            print("Animation complete.")

    return 0


if __name__ == "__main__":
    sys.exit(main())

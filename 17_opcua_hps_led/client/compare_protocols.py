#!/usr/bin/env python3
"""
compare_protocols.py — Quantitative & wire-level comparison between:
  1. Phase 7.0 (11_ethernet_hps_led): Raw 2-byte UDP protocol (port 5005)
  2. Phase 7.5 (11_ethernet_hps_led): Nanopb Protocol Buffers over UDP (port 5006)
  3. Phase 7.6 (11_ethernet_hps_led): WebSocket JSON Gateway (port 8080)
  4. Phase 11  (17_opcua_hps_led):    OPC UA Binary Client/Server (opc.tcp port 4840)
  5. Phase 11  (17_opcua_hps_led):    OPC UA Part 14 UADP PubSub over UDP (port 4841)

Generates exact wire byte encodings for `SET_PATTERN(0xA5)` and `GET_PATTERN()`
across all protocols and prints a side-by-side engineering comparison table.
"""

import json
import struct
from typing import Dict, Any

from opcua_client import (
    UA_SVC_BROWSE_REQ,
    UA_SVC_READ_REQ,
    UA_SVC_WRITE_REQ,
    UA_SVC_CALL_REQ,
    UA_TYPE_BYTE,
    UA_TYPE_UINT32,
    build_hel_chunk,
    build_opn_chunk,
    build_msg_chunk,
    encode_nodeid_string,
    encode_ua_string,
    encode_variant,
)


def hex_dump(data: bytes) -> str:
    return " ".join(f"{b:02x}" for b in data)


def build_protocol_samples(pattern: int = 0xA5) -> Dict[str, Dict[str, Any]]:
    """Constructs exact wire payloads for setting LED=0xA5 across all 5 protocols."""

    # 1. Phase 7.0: Raw 2-byte positional UDP (11_ethernet_hps_led/software/led_server/led_server.c)
    raw_udp_req  = struct.pack("BB", 0x01, pattern & 0xFF)  # [CMD_SET=0x01][0xA5]
    raw_udp_resp = struct.pack("BB", 0x00, pattern & 0xFF)  # [STATUS_OK=0x00][0xA5]

    # 2. Phase 7.5: Nanopb Protobuf over UDP (11_ethernet_hps_led/proto/led_command.proto)
    # In proto3: command=SET_PATTERN(0) is elided; field 2 (pattern=0xA5=165) -> tag 0x10 + varint(165)=0xa5 0x01
    pb_req  = bytes([0x10, (pattern & 0x7F) | 0x80, (pattern >> 7) & 0x7F]) if pattern >= 0x80 else bytes([0x10, pattern])
    # Response: status=OK(0) elided; field 2 (pattern=0xA5) -> 0x10 0xa5 0x01
    pb_resp = pb_req

    # 3. Phase 7.6: WebSocket / HTTP JSON Gateway (11_ethernet_hps_led/dashboard/gateway.py)
    ws_json_req  = json.dumps({"cmd": "SET_PATTERN", "pattern": pattern}, separators=(",", ":")).encode("utf-8")
    ws_json_resp = json.dumps({"status": "OK", "pattern": pattern, "hex": f"0x{pattern:02X}"}, separators=(",", ":")).encode("utf-8")

    # 4. Phase 11: OPC UA Client/Server (opc.tcp port 4840)
    opcua_hel = build_hel_chunk("opc.tcp://192.168.1.100:4840")
    opcua_opn = build_opn_chunk(0)
    opcua_write_req = build_msg_chunk(
        UA_SVC_WRITE_REQ,
        encode_nodeid_string(1, "fpga.led_pattern") + encode_variant(UA_TYPE_BYTE, pattern),
    )
    # MSGF(8) + SecHdr(16) + SvcNodeId(4: 01 00 a4 02) + StatusCode(4: 00 00 00 00) = 32 bytes
    opcua_write_resp = (
        b"MSGF"
        + struct.pack("<I", 32)
        + struct.pack("<IIII", 1, 1, 1, 1)
        + struct.pack("<BBHI", 0x01, 0x00, 676, 0x00000000)
    )
    opcua_read_req = build_msg_chunk(
        UA_SVC_READ_REQ,
        encode_nodeid_string(1, "fpga.led_pattern"),
    )
    opcua_call_req = build_msg_chunk(
        UA_SVC_CALL_REQ,
        encode_nodeid_string(1, "fpga.set_pattern") + encode_variant(UA_TYPE_UINT32, pattern),
    )
    opcua_browse_req = build_msg_chunk(
        UA_SVC_BROWSE_REQ,
        encode_ua_string("fpga"),
    )

    # 5. Phase 11: OPC UA Part 14 UADP Binary PubSub over UDP (port 4841)
    # 24-byte cyclic NetworkMessage containing PublisherId=0x000A, WriterId=1, SeqNum=1,
    # and 3 typed fields: LedPattern(Byte), OperationCount(UInt32), UptimeSeconds(UInt32)
    uadp_frame = (
        struct.pack("<BBHBHBHH", 0x11, 0x01, 0x000A, 1, 0x0001, 0x09, 1, 3)
        + encode_variant(UA_TYPE_BYTE, pattern)
        + encode_variant(UA_TYPE_UINT32, 42)
        + encode_variant(UA_TYPE_UINT32, 3600)
    )

    return {
        "raw_udp_7_0": {
            "name": "Phase 7.0 Raw UDP (11_ethernet_hps_led)",
            "transport": "UDP :5005",
            "req_bytes": len(raw_udp_req),
            "resp_bytes": len(raw_udp_resp),
            "req_hex": hex_dump(raw_udp_req),
            "resp_hex": hex_dump(raw_udp_resp),
            "self_describing": "No (positional bytes)",
            "browseable": "No",
            "push_telemetry": "No (client polls)",
        },
        "protobuf_7_5": {
            "name": "Phase 7.5 Nanopb Protobuf (11_ethernet_hps_led)",
            "transport": "UDP :5006",
            "req_bytes": len(pb_req),
            "resp_bytes": len(pb_resp),
            "req_hex": hex_dump(pb_req),
            "resp_hex": hex_dump(pb_resp),
            "self_describing": "Tag-only (.proto needed)",
            "browseable": "No",
            "push_telemetry": "No (client polls)",
        },
        "websocket_7_6": {
            "name": "Phase 7.6 WebSocket JSON (11_ethernet_hps_led)",
            "transport": "TCP/WS :8080",
            "req_bytes": len(ws_json_req),
            "resp_bytes": len(ws_json_resp),
            "req_hex": ws_json_req.decode("utf-8"),
            "resp_hex": ws_json_resp.decode("utf-8"),
            "self_describing": "Field names in JSON",
            "browseable": "No",
            "push_telemetry": "Yes (WebSocket push)",
        },
        "opcua_tcp_11": {
            "name": "Phase 11 OPC UA Client/Server (17_opcua_hps_led)",
            "transport": "TCP :4840 (opc.tcp)",
            "handshake_bytes": len(opcua_hel) + len(opcua_opn),
            "req_bytes": len(opcua_write_req),
            "resp_bytes": len(opcua_write_resp),
            "read_req_bytes": len(opcua_read_req),
            "call_req_bytes": len(opcua_call_req),
            "browse_req_bytes": len(opcua_browse_req),
            "req_hex": hex_dump(opcua_write_req),
            "resp_hex": hex_dump(opcua_write_resp),
            "self_describing": "Full Address Space + Types",
            "browseable": "Yes (Browse Service 527)",
            "push_telemetry": "Yes (MonitoredItems 829)",
        },
        "opcua_uadp_11": {
            "name": "Phase 11 OPC UA PubSub UADP (17_opcua_hps_led)",
            "transport": "UDP :4841 (UADP)",
            "req_bytes": 0,
            "resp_bytes": len(uadp_frame),
            "req_hex": "(connectionless publisher — 0 request bytes)",
            "resp_hex": hex_dump(uadp_frame),
            "self_describing": "Typed Variant fields",
            "browseable": "Via companion TCP server",
            "push_telemetry": "Yes (Cyclic UDP multicast)",
        },
    }


def print_comparison_report() -> None:
    samples = build_protocol_samples(0xA5)
    print("=" * 98)
    print("  WIRE PROTOCOL COMPARISON: 11_ethernet_hps_led (Phase 7) vs. 17_opcua_hps_led (Phase 11)")
    print("  Operation: Set FPGA LED PIO (0xFF200000) to pattern 0xA5 (165)")
    print("=" * 98)
    print(
        f"{'Protocol':<49} {'Transport':<18} {'Req':>5} {'Resp':>6} {'Browse?':<8} {'Push?'}"
    )
    print("-" * 98)
    for key in ("raw_udp_7_0", "protobuf_7_5", "websocket_7_6", "opcua_tcp_11", "opcua_uadp_11"):
        p = samples[key]
        b_short = "Yes" if p["browseable"].startswith("Yes") else "No"
        p_short = "Yes" if p["push_telemetry"].startswith("Yes") else "No"
        print(
            f"{p['name']:<49} {p['transport']:<18} "
            f"{p['req_bytes']:>4}B {p['resp_bytes']:>5}B "
            f"{b_short:<8} {p_short}"
        )
    print("-" * 98)
    print("\nWire Payload Breakdown for SET_PATTERN(0xA5):")
    for key in ("raw_udp_7_0", "protobuf_7_5", "opcua_tcp_11", "opcua_uadp_11"):
        p = samples[key]
        print(f"\n  * {p['name']}:")
        print(f"    TX ({p['req_bytes']:2d} B): {p['req_hex']}")
        print(f"    RX ({p['resp_bytes']:2d} B): {p['resp_hex']}")


if __name__ == "__main__":
    print_comparison_report()

#!/usr/bin/env python3
"""
test_gateway.py — Unit tests for gateway.py helper functions.

Tests the protobuf encoding/decoding and state-management logic without
requiring a WebSocket server, a UDP socket, or a running board.

Run with:
  python3 test_gateway.py          (from 11_ethernet_hps_led/dashboard/)
  python3 -m pytest test_gateway.py -v
  make test-dashboard              (from 11_ethernet_hps_led/)
"""

import sys
import unittest
from pathlib import Path

# Resolve sibling paths before importing gateway
_DASHBOARD = Path(__file__).parent
_CLIENT    = _DASHBOARD.parent / "client"
sys.path.insert(0, str(_CLIENT))
sys.path.insert(0, str(_DASHBOARD))

from led_command_pb2 import CommandType, LedCommand, LedResponse, StatusCode
from gateway import STATUS_NAMES, pb_decode, pb_encode


# ── Helpers ────────────────────────────────────────────────────────────────────

def _make_response(status: int, pattern: int) -> bytes:
    r = LedResponse()
    r.status  = status
    r.pattern = pattern
    return r.SerializeToString()


# ── pb_encode tests ────────────────────────────────────────────────────────────

class TestPbEncode(unittest.TestCase):
    """pb_encode() must produce valid nanopb-compatible serialised bytes."""

    def test_returns_bytes(self):
        self.assertIsInstance(pb_encode(CommandType.SET_PATTERN, 0xA5), bytes)

    def test_set_pattern_roundtrip(self):
        for val in [0x00, 0x01, 0xA5, 0xAA, 0x55, 0xFF]:
            data = pb_encode(CommandType.SET_PATTERN, val)
            msg  = LedCommand()
            msg.ParseFromString(data)
            self.assertEqual(msg.command, CommandType.SET_PATTERN)
            self.assertEqual(msg.pattern, val, f"Round-trip failed for 0x{val:02X}")

    def test_get_pattern_encodes(self):
        data = pb_encode(CommandType.GET_PATTERN)
        self.assertIsInstance(data, bytes)
        msg = LedCommand()
        msg.ParseFromString(data)
        self.assertEqual(msg.command, CommandType.GET_PATTERN)

    def test_pattern_masked_to_byte(self):
        """Values above 0xFF must be masked to 8 bits."""
        data = pb_encode(CommandType.SET_PATTERN, 0x1A5)
        msg  = LedCommand()
        msg.ParseFromString(data)
        self.assertEqual(msg.pattern, 0xA5)

    def test_all_patterns_roundtrip(self):
        for val in range(256):
            data = pb_encode(CommandType.SET_PATTERN, val)
            msg  = LedCommand()
            msg.ParseFromString(data)
            self.assertEqual(msg.pattern, val)

    def test_fits_nanopb_max_size(self):
        """Max LedCommand size according to generated pb.h is 8 bytes."""
        for pattern in [0x00, 0xFF, 0xA5]:
            data = pb_encode(CommandType.SET_PATTERN, pattern)
            self.assertLessEqual(len(data), 8,
                f"Encoded size {len(data)} exceeds nanopb limit for 0x{pattern:02X}")


# ── pb_decode tests ────────────────────────────────────────────────────────────

class TestPbDecode(unittest.TestCase):
    """pb_decode() must convert raw LedResponse bytes into a plain dict."""

    def test_ok_status(self):
        result = pb_decode(_make_response(StatusCode.OK, 0xA5))
        self.assertEqual(result["status"],  "OK")
        self.assertEqual(result["pattern"], 0xA5)

    def test_err_decode_fail(self):
        result = pb_decode(_make_response(StatusCode.ERR_DECODE_FAIL, 0x00))
        self.assertEqual(result["status"], "ERR_DECODE_FAIL")

    def test_err_unknown_cmd(self):
        result = pb_decode(_make_response(StatusCode.ERR_UNKNOWN_CMD, 0x00))
        self.assertEqual(result["status"], "ERR_UNKNOWN_CMD")

    def test_err_invalid_data(self):
        result = pb_decode(_make_response(StatusCode.ERR_INVALID_DATA, 0x00))
        self.assertEqual(result["status"], "ERR_INVALID_DATA")

    def test_unknown_status_returns_hex_string(self):
        result = pb_decode(_make_response(0x7F, 0x00))
        self.assertIn("7F", result["status"].upper())

    def test_all_patterns_preserved(self):
        for val in range(256):
            result = pb_decode(_make_response(StatusCode.OK, val))
            self.assertEqual(result["pattern"], val,
                f"Pattern round-trip failed for 0x{val:02X}")


# ── Full round-trip tests ──────────────────────────────────────────────────────

class TestRoundTrip(unittest.TestCase):
    """Encode a LedCommand and decode a matching LedResponse — full cycle."""

    def test_set_pattern_cycle(self):
        for val in [0x00, 0x01, 0xA5, 0xAA, 0x55, 0xFF]:
            # Encode request (as gateway would)
            req_bytes = pb_encode(CommandType.SET_PATTERN, val)
            # Decode request (as led_server_pb would)
            cmd = LedCommand()
            cmd.ParseFromString(req_bytes)
            # Build response (as led_server_pb would)
            resp_bytes = _make_response(StatusCode.OK, cmd.pattern)
            # Decode response (as gateway would)
            result = pb_decode(resp_bytes)
            self.assertEqual(result["status"],  "OK")
            self.assertEqual(result["pattern"], val)

    def test_get_pattern_cycle(self):
        req_bytes = pb_encode(CommandType.GET_PATTERN)
        cmd = LedCommand()
        cmd.ParseFromString(req_bytes)
        self.assertEqual(cmd.command, CommandType.GET_PATTERN)
        resp_bytes = _make_response(StatusCode.OK, 0xBE)
        result     = pb_decode(resp_bytes)
        self.assertEqual(result["pattern"], 0xBE)


# ── STATUS_NAMES coverage ──────────────────────────────────────────────────────

class TestStatusNames(unittest.TestCase):

    def test_all_known_codes_present(self):
        for code in (StatusCode.OK, StatusCode.ERR_UNKNOWN_CMD,
                     StatusCode.ERR_DECODE_FAIL, StatusCode.ERR_INVALID_DATA):
            self.assertIn(code, STATUS_NAMES)
            self.assertIsInstance(STATUS_NAMES[code], str)
            self.assertTrue(STATUS_NAMES[code])

    def test_ok_value(self):
        self.assertEqual(STATUS_NAMES[StatusCode.OK], "OK")


if __name__ == "__main__":
    unittest.main(verbosity=2)

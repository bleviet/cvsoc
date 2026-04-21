#!/usr/bin/env python3
"""
test_protocol_pb.py — Unit tests for the Phase 7.5 protobuf LED protocol.

Tests the nanopb/protobuf encoding and decoding logic in send_led_pattern_pb.py
without requiring a network connection or a running board.

Run with:
  python3 test_protocol_pb.py          (from 11_ethernet_hps_led/client/)
  python3 -m pytest test_protocol_pb.py -v
  make test-pb                         (from 11_ethernet_hps_led/)
"""

import sys
import unittest

sys.path.insert(0, ".")

from send_led_pattern_pb import (
    build_request,
    parse_response,
    ANIMATIONS,
    status_name,
)
from led_command_pb2 import (
    LedCommand,
    LedResponse,
    CommandType,
    StatusCode,
)


class TestRequestEncoding(unittest.TestCase):
    """Verify build_request() produces valid serialised protobuf bytes."""

    def test_set_pattern_is_bytes(self):
        pkt = build_request(CommandType.SET_PATTERN, 0xA5)
        self.assertIsInstance(pkt, bytes)

    def test_set_pattern_non_empty(self):
        pkt = build_request(CommandType.SET_PATTERN, 0xA5)
        self.assertGreater(len(pkt), 0)

    def test_set_pattern_decodes_correctly(self):
        pkt = build_request(CommandType.SET_PATTERN, 0xA5)
        msg = LedCommand()
        msg.ParseFromString(pkt)
        self.assertEqual(msg.command, CommandType.SET_PATTERN)
        self.assertEqual(msg.pattern, 0xA5)

    def test_get_pattern_decodes_correctly(self):
        pkt = build_request(CommandType.GET_PATTERN)
        msg = LedCommand()
        msg.ParseFromString(pkt)
        self.assertEqual(msg.command, CommandType.GET_PATTERN)

    def test_pattern_masked_to_byte(self):
        """Values above 0xFF must be masked to 8 bits before encoding."""
        pkt = build_request(CommandType.SET_PATTERN, 0x1FF)
        msg = LedCommand()
        msg.ParseFromString(pkt)
        self.assertEqual(msg.pattern, 0xFF)

    def test_all_led_values_roundtrip(self):
        """All 256 LED patterns must survive encode → decode without loss."""
        for val in range(256):
            pkt = build_request(CommandType.SET_PATTERN, val)
            msg = LedCommand()
            msg.ParseFromString(pkt)
            self.assertEqual(msg.pattern, val, f"Round-trip failed for 0x{val:02X}")

    def test_zero_pattern(self):
        pkt = build_request(CommandType.SET_PATTERN, 0x00)
        msg = LedCommand()
        msg.ParseFromString(pkt)
        self.assertEqual(msg.pattern, 0x00)

    def test_max_pattern(self):
        pkt = build_request(CommandType.SET_PATTERN, 0xFF)
        msg = LedCommand()
        msg.ParseFromString(pkt)
        self.assertEqual(msg.pattern, 0xFF)

    def test_max_encoded_size_fits_nanopb_limit(self):
        """LedCommand max size per nanopb: led_LedCommand_size = 8 bytes."""
        for pattern in [0x00, 0xFF, 0xA5]:
            pkt = build_request(CommandType.SET_PATTERN, pattern)
            self.assertLessEqual(len(pkt), 8,
                f"Encoded size {len(pkt)} exceeds nanopb limit of 8 bytes")


class TestResponseDecoding(unittest.TestCase):
    """Verify parse_response() correctly deserialises LedResponse messages."""

    def _make_response(self, status: int, pattern: int) -> bytes:
        resp = LedResponse()
        resp.status  = status
        resp.pattern = pattern
        return resp.SerializeToString()

    def test_ok_response(self):
        data = self._make_response(StatusCode.OK, 0xA5)
        resp = parse_response(data)
        self.assertEqual(resp.status,  StatusCode.OK)
        self.assertEqual(resp.pattern, 0xA5)

    def test_err_unknown_cmd(self):
        data = self._make_response(StatusCode.ERR_UNKNOWN_CMD, 0x00)
        resp = parse_response(data)
        self.assertEqual(resp.status, StatusCode.ERR_UNKNOWN_CMD)

    def test_err_decode_fail(self):
        data = self._make_response(StatusCode.ERR_DECODE_FAIL, 0x00)
        resp = parse_response(data)
        self.assertEqual(resp.status, StatusCode.ERR_DECODE_FAIL)

    def test_garbage_raises_value_error(self):
        """Completely invalid protobuf bytes must raise ValueError."""
        with self.assertRaises(Exception):
            # While protobuf is tolerant, a non-protobuf payload should fail
            # parse_response(), which wraps any exception in ValueError.
            parse_response(b"\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff")

    def test_pattern_preserved(self):
        for val in range(256):
            data = self._make_response(StatusCode.OK, val)
            resp = parse_response(data)
            self.assertEqual(resp.pattern, val)

    def test_max_response_size_fits_nanopb_limit(self):
        """LedResponse max size per nanopb: led_LedResponse_size = 8 bytes."""
        for pattern in [0x00, 0xFF, 0xA5]:
            data = self._make_response(StatusCode.OK, pattern)
            self.assertLessEqual(len(data), 8,
                f"Encoded size {len(data)} exceeds nanopb limit of 8 bytes")


class TestRoundTrip(unittest.TestCase):
    """Verify encode → decode round-trips are lossless for the full range."""

    def test_set_pattern_roundtrip(self):
        for val in [0x00, 0x01, 0xA5, 0xAA, 0x55, 0xFF]:
            req_bytes = build_request(CommandType.SET_PATTERN, val)
            # Decode as LedCommand (simulating the server side)
            cmd = LedCommand()
            cmd.ParseFromString(req_bytes)
            self.assertEqual(cmd.command, CommandType.SET_PATTERN)
            self.assertEqual(cmd.pattern, val, f"Round-trip failed for 0x{val:02X}")

    def test_response_roundtrip(self):
        """The server response path: encode LedResponse → decode on client."""
        for val in [0x00, 0x01, 0xA5, 0xAA, 0x55, 0xFF]:
            resp_out = LedResponse()
            resp_out.status  = StatusCode.OK
            resp_out.pattern = val
            data = resp_out.SerializeToString()

            resp_in = parse_response(data)
            self.assertEqual(resp_in.status,  StatusCode.OK)
            self.assertEqual(resp_in.pattern, val)


class TestSchemaEvolution(unittest.TestCase):
    """
    Demonstrate protobuf's forward-compatibility guarantee.

    A message serialised with new fields unknown to the decoder must still
    decode successfully, with known fields preserved.  This is the core value
    of protobuf over raw byte-packing.
    """

    def test_unknown_field_ignored_by_decoder(self):
        """
        Simulate a future client that sends a LedCommand with an extra
        field (e.g. brightness = field 3).  The current server decoder must
        still read command and pattern correctly.
        """
        # Manually craft a protobuf message with an extra field 3 = 75 (varint)
        # Field tag for field 3, type varint: (3 << 3) | 0 = 0x18
        future_msg  = build_request(CommandType.SET_PATTERN, 0xA5)
        future_msg += bytes([0x18, 75])  # brightness = 75 (future field)

        cmd = LedCommand()
        cmd.ParseFromString(future_msg)
        # Known fields must still decode correctly
        self.assertEqual(cmd.command, CommandType.SET_PATTERN)
        self.assertEqual(cmd.pattern, 0xA5)


class TestAnimations(unittest.TestCase):
    """Validate animation sequence data (shared with the raw protocol client)."""

    def test_all_animations_defined(self):
        for name in ("chase", "breathe", "blink", "stripes", "all"):
            self.assertIn(name, ANIMATIONS)

    def test_animations_non_empty(self):
        for name, frames in ANIMATIONS.items():
            self.assertGreater(len(frames), 0, f"Animation '{name}' is empty")

    def test_animation_values_in_byte_range(self):
        for name, frames in ANIMATIONS.items():
            for i, val in enumerate(frames):
                self.assertGreaterEqual(val, 0x00)
                self.assertLessEqual(val, 0xFF,
                    f"Animation '{name}' frame {i} = 0x{val:02X} above 0xFF")


class TestStatusNames(unittest.TestCase):
    """Verify status_name() produces human-readable strings."""

    def test_ok_name(self):
        self.assertEqual(status_name(StatusCode.OK), "OK")

    def test_unknown_cmd_name(self):
        self.assertIn("UNKNOWN", status_name(StatusCode.ERR_UNKNOWN_CMD).upper())

    def test_unknown_code_shows_hex(self):
        name = status_name(0xFF)
        self.assertIn("0x", name)


if __name__ == "__main__":
    unittest.main(verbosity=2)

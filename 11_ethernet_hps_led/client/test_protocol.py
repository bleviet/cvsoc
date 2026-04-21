#!/usr/bin/env python3
"""
test_protocol.py — Unit tests for the Phase 7 UDP LED server protocol.

Tests the packet encoding/decoding logic defined in send_led_pattern.py
without requiring a network connection or a running board.

Run with:
  python3 test_protocol.py
  python3 -m pytest test_protocol.py -v
"""

import sys
import unittest

# Import the protocol helpers from the client module
sys.path.insert(0, ".")
from send_led_pattern import (
    build_request,
    parse_response,
    CMD_SET_PATTERN,
    CMD_GET_PATTERN,
    STATUS_OK,
    STATUS_ERR_UNKNOWN_CMD,
    STATUS_ERR_INVALID_LEN,
    ANIMATIONS,
)


class TestRequestEncoding(unittest.TestCase):
    """Verify build_request() produces correct 2-byte packets."""

    def test_set_pattern_length(self):
        pkt = build_request(CMD_SET_PATTERN, 0xA5)
        self.assertEqual(len(pkt), 2)

    def test_set_pattern_cmd_byte(self):
        pkt = build_request(CMD_SET_PATTERN, 0xA5)
        self.assertEqual(pkt[0], CMD_SET_PATTERN)

    def test_set_pattern_data_byte(self):
        pkt = build_request(CMD_SET_PATTERN, 0xA5)
        self.assertEqual(pkt[1], 0xA5)

    def test_get_pattern_cmd(self):
        pkt = build_request(CMD_GET_PATTERN)
        self.assertEqual(pkt[0], CMD_GET_PATTERN)
        self.assertEqual(pkt[1], 0x00)

    def test_pattern_masked_to_byte(self):
        """Values above 0xFF must be masked to 8 bits."""
        pkt = build_request(CMD_SET_PATTERN, 0x1FF)
        self.assertEqual(pkt[1], 0xFF)

    def test_all_led_values(self):
        """All 256 LED patterns must round-trip through encoding."""
        for val in range(256):
            pkt = build_request(CMD_SET_PATTERN, val)
            self.assertEqual(pkt[1], val)

    def test_zero_pattern(self):
        pkt = build_request(CMD_SET_PATTERN, 0x00)
        self.assertEqual(pkt, bytes([CMD_SET_PATTERN, 0x00]))

    def test_full_pattern(self):
        pkt = build_request(CMD_SET_PATTERN, 0xFF)
        self.assertEqual(pkt, bytes([CMD_SET_PATTERN, 0xFF]))


class TestResponseDecoding(unittest.TestCase):
    """Verify parse_response() correctly unpacks 2-byte responses."""

    def test_ok_response(self):
        status, pattern = parse_response(bytes([STATUS_OK, 0xA5]))
        self.assertEqual(status, STATUS_OK)
        self.assertEqual(pattern, 0xA5)

    def test_error_unknown_cmd(self):
        status, pattern = parse_response(bytes([STATUS_ERR_UNKNOWN_CMD, 0x00]))
        self.assertEqual(status, STATUS_ERR_UNKNOWN_CMD)

    def test_error_invalid_len(self):
        status, _ = parse_response(bytes([STATUS_ERR_INVALID_LEN, 0x00]))
        self.assertEqual(status, STATUS_ERR_INVALID_LEN)

    def test_wrong_response_length_raises(self):
        with self.assertRaises(ValueError):
            parse_response(bytes([0x00]))  # 1 byte instead of 2

    def test_wrong_response_length_raises_3bytes(self):
        with self.assertRaises(ValueError):
            parse_response(bytes([0x00, 0x00, 0x00]))

    def test_pattern_preserved(self):
        for val in range(256):
            _, pattern = parse_response(bytes([STATUS_OK, val]))
            self.assertEqual(pattern, val)


class TestRoundTrip(unittest.TestCase):
    """Verify encode→decode round-trips are lossless."""

    def test_set_pattern_roundtrip(self):
        """Encoding a request and then treating the echo as a response
        should preserve the pattern value."""
        for val in [0x00, 0x01, 0xA5, 0xAA, 0x55, 0xFF]:
            req = build_request(CMD_SET_PATTERN, val)
            # Simulate the server echoing status=OK + the pattern it received
            fake_resp = bytes([STATUS_OK, req[1]])
            status, echo = parse_response(fake_resp)
            self.assertEqual(status, STATUS_OK)
            self.assertEqual(echo, val, f"Round-trip failed for 0x{val:02X}")


class TestAnimations(unittest.TestCase):
    """Validate animation sequence data."""

    def test_all_animations_defined(self):
        for name in ("chase", "breathe", "blink", "stripes", "all"):
            self.assertIn(name, ANIMATIONS)

    def test_animations_non_empty(self):
        for name, frames in ANIMATIONS.items():
            self.assertGreater(len(frames), 0, f"Animation '{name}' is empty")

    def test_animation_values_in_byte_range(self):
        for name, frames in ANIMATIONS.items():
            for i, val in enumerate(frames):
                self.assertGreaterEqual(val, 0x00,
                    f"Animation '{name}' frame {i} = 0x{val:02X} below 0")
                self.assertLessEqual(val, 0xFF,
                    f"Animation '{name}' frame {i} = 0x{val:02X} above 0xFF")

    def test_chase_starts_lsb(self):
        """Chase animation starts with bit 0 set."""
        self.assertEqual(ANIMATIONS["chase"][0], 0x01)

    def test_blink_alternates(self):
        """Blink animation alternates full-on and full-off."""
        frames = ANIMATIONS["blink"]
        self.assertEqual(frames[0], 0xFF)
        self.assertEqual(frames[1], 0x00)

    def test_stripes_complementary(self):
        """Stripe frames should be bitwise complements of each other."""
        frames = ANIMATIONS["stripes"]
        self.assertEqual(frames[0] ^ frames[1], 0xFF)


if __name__ == "__main__":
    unittest.main(verbosity=2)

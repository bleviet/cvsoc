#!/usr/bin/env python3
"""
test_opcua.py — Automated unit and end-to-end integration test suite for
                Phase 11 (17_opcua_hps_led).

Spawns the compiled C `led_opcua_server --sim` daemon on localhost and verifies:
  1. HEL / ACK / OPN OPC UA Binary connection handshake
  2. Dynamic Address Space discovery via Browse Service (527)
  3. Read (631) and Write (673) on `ns=1;s=fpga.led_pattern`
  4. Bit-level access via `ns=1;s=fpga.led0` .. `ns=1;s=fpga.led7`
  5. Remote Procedure Calls (712) via `fpga.set_pattern` & `fpga.invert_pattern`
  6. OPC UA StatusCodes (BadNodeIdUnknown, BadNotWritable, BadOutOfRange, BadTypeMismatch)
  7. Report-by-Exception Subscriptions (`CreateSubscription` 787 & `Publish` 829)
  8. OPC UA Part 14 UADP Binary PubSub UDP telemetry frame decoding
  9. Quantitative wire comparison against Phase 7 (`11_ethernet_hps_led`)
"""

import os
import socket
import subprocess
import sys
import time
import unittest

THIS_DIR   = os.path.dirname(os.path.abspath(__file__))
PROJ_DIR   = os.path.dirname(THIS_DIR)
SERVER_DIR = os.path.join(PROJ_DIR, "software", "opcua_server")
SERVER_BIN = os.path.join(SERVER_DIR, "led_opcua_server_sim")

sys.path.insert(0, THIS_DIR)

from opcua_client import (
    OpcUaClient,
    UA_NODECLASS_METHOD,
    UA_NODECLASS_OBJECT,
    UA_NODECLASS_VARIABLE,
    UA_STATUSCODE_BADNODEIDUNKNOWN,
    UA_STATUSCODE_BADNOTWRITABLE,
    UA_STATUSCODE_BADOUTOFRANGE,
    UA_STATUSCODE_BADTYPEMISMATCH,
    UA_STATUSCODE_GOOD,
    UA_TYPE_BOOLEAN,
    UA_TYPE_BYTE,
    UA_TYPE_STRING,
    UA_TYPE_UINT32,
    decode_uadp_frame,
)
from compare_protocols import build_protocol_samples


def find_free_ports() -> tuple[int, int]:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s1:
        s1.bind(("127.0.0.1", 0))
        tcp_port = s1.getsockname()[1]
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as s2:
        s2.bind(("127.0.0.1", 0))
        udp_port = s2.getsockname()[1]
    return tcp_port, udp_port


class TestDe10NanoOpcUa(unittest.TestCase):
    proc: subprocess.Popen
    tcp_port: int
    udp_port: int

    @classmethod
    def setUpClass(cls) -> None:
        subprocess.run(["make", "-C", SERVER_DIR, "sim"], check=True, stdout=subprocess.PIPE)
        cls.tcp_port, cls.udp_port = find_free_ports()
        cls.proc = subprocess.Popen(
            [
                SERVER_BIN,
                "--sim",
                "--port", str(cls.tcp_port),
                "--pubsub-port", str(cls.udp_port),
                "--interval-ms", "100",
            ],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )
        # Wait for TCP port to accept connections
        deadline = time.time() + 5.0
        while time.time() < deadline:
            try:
                with OpcUaClient("127.0.0.1", cls.tcp_port, timeout=0.5) as c:
                    status, _, _ = c.read("fpga.led_pattern")
                    if status == UA_STATUSCODE_GOOD:
                        return
            except Exception:
                time.sleep(0.05)
        raise RuntimeError("led_opcua_server failed to start within 5 seconds")

    @classmethod
    def tearDownClass(cls) -> None:
        if cls.proc:
            if cls.proc.poll() is None:
                cls.proc.terminate()
                try:
                    cls.proc.wait(timeout=3)
                except subprocess.TimeoutExpired:
                    cls.proc.kill()
            if cls.proc.stdout:
                cls.proc.stdout.close()
            if cls.proc.stderr:
                cls.proc.stderr.close()

    def test_01_browse_address_space_discovery(self) -> None:
        """Browse (527) discovers Objects, Variables, and Methods without a .proto file."""
        with OpcUaClient("127.0.0.1", self.tcp_port) as client:
            all_nodes = client.browse()
            self.assertEqual(len(all_nodes), 17)

            by_id = {n.identifier: n for n in all_nodes}
            self.assertIn("de10nano", by_id)
            self.assertEqual(by_id["de10nano"].node_class, UA_NODECLASS_OBJECT)

            self.assertIn("fpga.led_pattern", by_id)
            self.assertEqual(by_id["fpga.led_pattern"].node_class, UA_NODECLASS_VARIABLE)
            self.assertEqual(by_id["fpga.led_pattern"].data_type, UA_TYPE_BYTE)

            self.assertIn("fpga.set_pattern", by_id)
            self.assertEqual(by_id["fpga.set_pattern"].node_class, UA_NODECLASS_METHOD)

            self.assertIn("fpga.invert_pattern", by_id)
            self.assertEqual(by_id["fpga.invert_pattern"].node_class, UA_NODECLASS_METHOD)

            # Filtered browse of 'diag' parent object returns only the 2 diagnostic nodes
            diag_nodes = client.browse("diag")
            self.assertEqual({n.identifier for n in diag_nodes}, {"diag.op_count", "diag.uptime_sec"})

    def test_02_read_and_write_led_pattern_register(self) -> None:
        """Read (631) and Write (673) update the simulated 0xFF200000 LED register."""
        with OpcUaClient("127.0.0.1", self.tcp_port) as client:
            for pattern in (0xA5, 0x5A, 0x00, 0xFF, 0x3C):
                st = client.write("ns=1;s=fpga.led_pattern", pattern, UA_TYPE_BYTE)
                self.assertEqual(st, UA_STATUSCODE_GOOD)

                r_st, vtype, val = client.read("ns=1;s=fpga.led_pattern")
                self.assertEqual(r_st, UA_STATUSCODE_GOOD)
                self.assertEqual(vtype, UA_TYPE_BYTE)
                self.assertEqual(val, pattern)

    def test_03_individual_led_bit_nodes(self) -> None:
        """Writing `fpga.led0`..`fpga.led7` sets/clears individual bits in `fpga.led_pattern`."""
        with OpcUaClient("127.0.0.1", self.tcp_port) as client:
            client.write("fpga.led_pattern", 0x00, UA_TYPE_BYTE)

            # Turn on LED[0] and LED[7] -> 0x81
            self.assertEqual(client.write("fpga.led0", True, UA_TYPE_BOOLEAN), UA_STATUSCODE_GOOD)
            self.assertEqual(client.write("fpga.led7", True, UA_TYPE_BOOLEAN), UA_STATUSCODE_GOOD)

            _, _, pat = client.read("fpga.led_pattern")
            self.assertEqual(pat, 0x81)

            _, _, b0 = client.read("fpga.led0")
            _, _, b1 = client.read("fpga.led1")
            _, _, b7 = client.read("fpga.led7")
            self.assertTrue(b0)
            self.assertFalse(b1)
            self.assertTrue(b7)

    def test_04_method_calls_set_and_invert(self) -> None:
        """Call (712) executes `fpga.set_pattern` and `fpga.invert_pattern` on the server."""
        with OpcUaClient("127.0.0.1", self.tcp_port) as client:
            _, _, ops_before = client.read("diag.op_count")

            st, out_val = client.call("fpga.set_pattern", 0xF0, UA_TYPE_UINT32)
            self.assertEqual(st, UA_STATUSCODE_GOOD)
            self.assertEqual(out_val, 0xF0)

            st2, out_inv = client.call("fpga.invert_pattern")
            self.assertEqual(st2, UA_STATUSCODE_GOOD)
            self.assertEqual(out_inv, 0x0F)

            _, _, ops_after = client.read("diag.op_count")
            self.assertEqual(ops_after, ops_before + 2)

    def test_05_read_only_and_status_code_validation(self) -> None:
        """Server enforces standard OPC UA StatusCodes on invalid NodeIds, types, and ranges."""
        with OpcUaClient("127.0.0.1", self.tcp_port) as client:
            # 1. Unknown NodeId -> BadNodeIdUnknown (0x80340000)
            st, _, _ = client.read("fpga.nonexistent_register")
            self.assertEqual(st, UA_STATUSCODE_BADNODEIDUNKNOWN)

            # 2. Read-only node `fpga.bitstream_state` -> readable, but BadNotWritable (0x803B0000)
            st_r, vtype, bs_state = client.read("fpga.bitstream_state")
            self.assertEqual(st_r, UA_STATUSCODE_GOOD)
            self.assertEqual(vtype, UA_TYPE_STRING)
            self.assertIn("operating", bs_state)

            st_w = client.write("fpga.bitstream_state", "hacked", UA_TYPE_STRING)
            self.assertEqual(st_w, UA_STATUSCODE_BADNOTWRITABLE)

            # 3. Wrong Variant type on `fpga.led_pattern` -> BadTypeMismatch (0x80740000)
            st_type = client.write("fpga.led_pattern", "not_a_byte", UA_TYPE_STRING)
            self.assertEqual(st_type, UA_STATUSCODE_BADTYPEMISMATCH)

            # 4. Out-of-range argument (> 0xFF) on `fpga.set_pattern` -> BadOutOfRange (0x803C0000)
            st_range, _ = client.call("fpga.set_pattern", 0x1FF, UA_TYPE_UINT32)
            self.assertEqual(st_range, UA_STATUSCODE_BADOUTOFRANGE)

    def test_06_monitored_item_subscription_push(self) -> None:
        """Subscribing client receives immediate initial notification + push on state change."""
        with OpcUaClient("127.0.0.1", self.tcp_port) as sub_client, \
             OpcUaClient("127.0.0.1", self.tcp_port) as writer_client:

            writer_client.write("fpga.led_pattern", 0x12, UA_TYPE_BYTE)

            st, sub_id = sub_client.create_subscription("fpga.led_pattern")
            self.assertEqual(st, UA_STATUSCODE_GOOD)
            self.assertEqual(sub_id, 1)

            # 1. Initial value pushed immediately upon subscription creation
            nid1, val1 = sub_client.wait_for_notification()
            self.assertEqual(nid1, "ns=1;s=fpga.led_pattern")
            self.assertEqual(val1, 0x12)

            # 2. Second client changes LED pattern -> server pushes DataChangeNotification
            writer_client.write("fpga.led_pattern", 0xAB, UA_TYPE_BYTE)
            nid2, val2 = sub_client.wait_for_notification()
            self.assertEqual(nid2, "ns=1;s=fpga.led_pattern")
            self.assertEqual(val2, 0xAB)

    def test_07_part14_uadp_pubsub_telemetry(self) -> None:
        """Server broadcasts valid 24-byte OPC UA Part 14 UADP frames over UDP."""
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as udp_sock:
            udp_sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            udp_sock.settimeout(2.0)
            udp_sock.bind(("127.0.0.1", self.udp_port))

            with OpcUaClient("127.0.0.1", self.tcp_port) as client:
                client.write("fpga.led_pattern", 0xC3, UA_TYPE_BYTE)

            raw, _ = udp_sock.recvfrom(128)
            decoded = decode_uadp_frame(raw)
            self.assertEqual(decoded["uadp_version"], 1)
            self.assertEqual(decoded["publisher_id"], 0x000A)
            self.assertEqual(decoded["dataset_writer_id"], 0x0001)
            self.assertEqual(decoded["field_count"], 3)
            self.assertEqual(decoded["led_pattern"], 0xC3)
            self.assertEqual(decoded["wire_bytes"], 24)

    def test_08_protocol_comparison_vs_phase7(self) -> None:
        """Verifies wire size metrics comparing Phase 7 (11_ethernet_hps_led) and Phase 11."""
        samples = build_protocol_samples(0xA5)
        self.assertEqual(samples["raw_udp_7_0"]["req_bytes"], 2)
        self.assertEqual(samples["raw_udp_7_0"]["resp_bytes"], 2)
        self.assertEqual(samples["protobuf_7_5"]["req_bytes"], 3)
        self.assertEqual(samples["protobuf_7_5"]["resp_bytes"], 3)
        self.assertEqual(samples["opcua_tcp_11"]["req_bytes"], 53)
        self.assertEqual(samples["opcua_tcp_11"]["resp_bytes"], 32)
        self.assertEqual(samples["opcua_uadp_11"]["resp_bytes"], 24)


if __name__ == "__main__":
    unittest.main(verbosity=2)

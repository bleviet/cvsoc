#!/usr/bin/env python3
"""camd protocol tests in --sim mode, on 127.0.0.1 (no board, no Aravis).

Covers discovery, the GenICam XML and its register map, control-channel
privilege, heartbeat expiry, parameter validation, and GVSP streaming checked
pixel-for-pixel against the test-pattern model. Run via `make test`.
"""

import os
import pathlib
import re
import shutil
import struct
import subprocess
import tempfile
import time
import unittest
import xml.etree.ElementTree as ET

import gev
import tpg_model

PROJECT = pathlib.Path(__file__).resolve().parent.parent
CAMD = PROJECT / "software" / "camd" / "build" / "host" / "camd"
XML_FILE = PROJECT / "software" / "camd" / "genicam" / "cvsoc_camera.xml"
CAMERA_H = PROJECT / "software" / "camd" / "camera.h"
PORT = 39561
SENSOR_W, SENSOR_H = 640, 480
NS = "{http://www.genicam.org/GenApi/Version_1_1}"


class CamdTest(unittest.TestCase):

    @classmethod
    def setUpClass(cls):
        cls.proc = subprocess.Popen(
            [str(CAMD), "--sim", "-p", str(PORT), "-W", str(SENSOR_W), "-H", str(SENSOR_H)],
            stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, text=True)
        line = cls.proc.stderr.readline()
        if "GVCP on UDP" not in line:
            cls.proc.kill()
            raise RuntimeError(f"camd did not start: {line!r}")

    @classmethod
    def tearDownClass(cls):
        cls.proc.terminate()
        cls.proc.wait(timeout=5)

    def setUp(self):
        self.cam = gev.Gvcp("127.0.0.1", PORT)
        self.cam.take_control()
        self.cam.write_reg(gev.BS_HEARTBEAT, 3000)

    def tearDown(self):
        try:
            gev.stop_stream(self.cam)
            # Offsets first: Width/Height may only grow once the offsets are 0.
            for reg, value in (("OFFSET_X", 0), ("OFFSET_Y", 0), ("WIDTH", SENSOR_W),
                               ("HEIGHT", SENSOR_H), ("PIXEL_FORMAT", gev.BAYER_GR8),
                               ("TEST_PATTERN", 1)):
                self.cam.write_reg(gev.REG[reg], value)
            self.cam.write_float(gev.REG["FRAME_RATE"], 10.0)
        finally:
            try:
                self.cam.release()
            finally:
                self.cam.close()

    def assertStatus(self, status, fn, *args):
        with self.assertRaises(gev.GevError) as ctx:
            fn(*args)
        self.assertEqual(ctx.exception.status, status)

    # ── Discovery and identity ────────────────────────────────────────────
    def test_discovery_identity(self):
        info = self.cam.discover()
        self.assertEqual(info["version"], "1.2")
        self.assertEqual(info["manufacturer"], "cvsoc")
        self.assertEqual(info["model"], "DE10-Nano GigE Camera")
        self.assertEqual(info["serial"], "CVSOC-SIM")
        self.assertEqual(info["ip"], "127.0.0.1")
        self.assertTrue(info["device_mode"] & 0x80000000, "big-endian device mode bit")

    def test_user_name_writemem(self):
        self.cam.write_mem(0xE8, b"bench-cam".ljust(16, b"\0"))
        self.assertEqual(self.cam.discover()["user_name"], "bench-cam")
        self.cam.write_mem(0xE8, b"\0" * 16)

    # ── GenICam XML ───────────────────────────────────────────────────────
    def test_xml_served_matches_source(self):
        name, data = self.cam.xml()
        self.assertEqual(name, "cvsoc_camera.xml")
        self.assertEqual(data, XML_FILE.read_bytes())

    def test_xml_features_present(self):
        root = ET.fromstring(XML_FILE.read_bytes())
        names = {n.get("Name") for n in root}
        for feature in ("Root", "SensorWidth", "SensorHeight", "Width", "Height",
                        "OffsetX", "OffsetY", "PixelFormat",
                        "PayloadSize", "AcquisitionMode", "AcquisitionStart",
                        "AcquisitionStop", "AcquisitionFrameRate", "TestPattern",
                        "GevSCPSPacketSize", "TLParamsLocked", "DeviceVendorName",
                        "DeviceModelName", "DeviceSerialNumber", "Device"):
            self.assertIn(feature, names)
        # Every pValue / pFeature / pVariable / pMax must name a node.
        for el in root.iter():
            tag = el.tag.replace(NS, "")
            if tag in ("pValue", "pFeature", "pVariable", "pMax", "pPort"):
                self.assertIn(el.text, names, f"{tag} -> {el.text}")

    def test_xml_validates_against_genapi_1_1(self):
        """Schema check with the GenApi 1.1 XSD (from GENAPI_XSD, or the copy
        bundled in Aravis's arv-test). Skipped when neither is available."""
        xsd = os.environ.get("GENAPI_XSD")
        tmp = None
        if not xsd and shutil.which("gresource") and shutil.which("arv-test-0.8"):
            tmp = tempfile.NamedTemporaryFile(suffix=".xsd", delete=False)
            subprocess.run(["gresource", "extract", shutil.which("arv-test-0.8"),
                            "/org/aravis/GenApiSchema_Version_1_1.xsd"],
                           stdout=tmp, check=True)
            tmp.close()
            xsd = tmp.name
        if not xsd or not shutil.which("xmllint"):
            self.skipTest("needs xmllint and GENAPI_XSD or Aravis's arv-test-0.8")
        try:
            res = subprocess.run(["xmllint", "--noout", "--schema", xsd, str(XML_FILE)],
                                 capture_output=True, text=True)
            self.assertEqual(res.returncode, 0, res.stderr)
        finally:
            if tmp:
                os.unlink(tmp.name)

    def test_xml_addresses_match_camera_h(self):
        header = CAMERA_H.read_text()
        defined = {int(v, 16) for v in re.findall(r"#define REG_\w+\s+(0x[0-9A-Fa-f]+)", header)}
        root = ET.fromstring(XML_FILE.read_bytes())
        for node in root:
            addr = node.find(f"{NS}Address")
            if addr is None:
                continue
            a = int(addr.text, 16)
            if a >= 0x10000:
                self.assertIn(a, defined, f"{node.get('Name')} at 0x{a:x} not in camera.h")
            if node.find(f"{NS}AccessMode").text != "WO":
                length = int(node.find(f"{NS}Length").text)
                self.cam.read_mem(a, length)   # must not raise

    # ── Control channel privilege and heartbeat ───────────────────────────
    def test_write_requires_control(self):
        other = gev.Gvcp("127.0.0.1", PORT)
        try:
            self.assertStatus(gev.ACCESS_DENIED, other.write_reg, gev.REG["WIDTH"], 320)
            self.assertStatus(gev.ACCESS_DENIED, other.take_control)
            self.assertEqual(other.read_reg(gev.REG["WIDTH"]), SENSOR_W)   # reads allowed
        finally:
            other.close()

    def test_release_lets_another_client_take_control(self):
        self.cam.release()
        other = gev.Gvcp("127.0.0.1", PORT)
        try:
            other.take_control()
            self.assertStatus(gev.ACCESS_DENIED, self.cam.take_control)
            other.release()
        finally:
            other.close()
        self.cam.take_control()

    def test_heartbeat_expiry_releases_control(self):
        self.cam.write_reg(gev.BS_HEARTBEAT, 500)
        time.sleep(0.9)
        other = gev.Gvcp("127.0.0.1", PORT)
        try:
            other.take_control()
            other.release()
        finally:
            other.close()
        self.cam.take_control()

    # ── Parameter validation ──────────────────────────────────────────────
    def test_validation(self):
        reg = gev.REG
        self.assertStatus(gev.INVALID_PARAMETER, self.cam.write_reg, reg["WIDTH"], 100)
        self.assertStatus(gev.INVALID_PARAMETER, self.cam.write_reg, reg["WIDTH"], SENSOR_W + 8)
        self.assertStatus(gev.INVALID_PARAMETER, self.cam.write_reg, reg["OFFSET_Y"], 1)
        self.assertStatus(gev.INVALID_PARAMETER, self.cam.write_reg, reg["PIXEL_FORMAT"], 0x1234)
        self.assertStatus(gev.INVALID_PARAMETER, self.cam.write_float, reg["FRAME_RATE"], 500.0)
        self.assertStatus(gev.WRITE_PROTECT, self.cam.write_reg, reg["PAYLOAD_SIZE"], 1)
        self.assertStatus(gev.BAD_ALIGNMENT, self.cam.write_reg, reg["WIDTH"] + 1, 1)
        self.assertStatus(gev.INVALID_ADDRESS, self.cam.read_reg, 0x20000)

    def test_payload_size_follows_roi(self):
        self.cam.write_reg(gev.REG["WIDTH"], 320)
        self.cam.write_reg(gev.REG["HEIGHT"], 100)
        self.assertEqual(self.cam.read_reg(gev.REG["PAYLOAD_SIZE"]), 320 * 100)

    def test_packet_size_is_clamped_and_aligned(self):
        self.cam.write_reg(gev.BS_SCPS0, 9000)
        self.assertEqual(self.cam.read_reg(gev.BS_SCPS0) & 0xFFFF, 3796)
        self.cam.write_reg(gev.BS_SCPS0, 1499)
        self.assertEqual(self.cam.read_reg(gev.BS_SCPS0) & 0xFFFF, 1496)

    # ── Streaming ─────────────────────────────────────────────────────────
    def grab(self, count, packet_size=1500):
        rx = gev.Gvsp("127.0.0.1")
        try:
            gev.start_stream(self.cam, rx, "127.0.0.1", packet_size)
            return rx.frames(count)
        finally:
            gev.stop_stream(self.cam)
            rx.close()

    def check_frames(self, frames, pattern, fmt, w, h, ox, oy):
        for f in frames:
            self.assertTrue(f["complete"])
            self.assertEqual((f["pixel_format"], f["width"], f["height"]), (fmt, w, h))
            self.assertEqual((f["offset_x"], f["offset_y"]), (ox, oy))
            self.assertEqual(f["trailer_height"], h)
            n = (f["data"][0] - ox) & 0xFF if pattern == tpg_model.GREY_HRAMP_MOVING else 0
            self.assertEqual(f["data"], tpg_model.frame(pattern, fmt, w, h, ox, oy, n, SENSOR_W))

    def test_stream_full_frame_ramp(self):
        frames = self.grab(3)
        self.check_frames(frames, tpg_model.GREY_HRAMP_MOVING, gev.BAYER_GR8,
                          SENSOR_W, SENSOR_H, 0, 0)
        firsts = [f["data"][0] for f in frames]
        self.assertEqual([(b - firsts[0]) & 0xFF for b in firsts], [0, 1, 2], "consecutive frames")
        ids = [f["block_id"] for f in frames]
        self.assertEqual(ids, list(range(ids[0], ids[0] + 3)))

    def test_stream_roi_color_bar_bayer_and_mono(self):
        for fmt in (gev.BAYER_GR8, gev.MONO8):
            with self.subTest(pixel_format=hex(fmt)):
                self.cam.write_reg(gev.REG["WIDTH"], 320)
                self.cam.write_reg(gev.REG["HEIGHT"], 120)
                self.cam.write_reg(gev.REG["OFFSET_X"], 160)
                self.cam.write_reg(gev.REG["OFFSET_Y"], 40)
                self.cam.write_reg(gev.REG["PIXEL_FORMAT"], fmt)
                self.cam.write_reg(gev.REG["TEST_PATTERN"], tpg_model.COLOR_BAR)
                self.check_frames(self.grab(1), tpg_model.COLOR_BAR, fmt, 320, 120, 160, 40)

    def test_stream_packet_sizes(self):
        for size in (1500, 3796):
            with self.subTest(packet_size=size):
                f = self.grab(1, size)[0]
                data = size - 36
                self.assertTrue(all(s == data for s in f["packet_sizes"][:-1]))
                self.assertEqual(len(f["packet_sizes"]), -(-SENSOR_W * SENSOR_H // data))

    def test_image_registers_busy_while_acquiring(self):
        rx = gev.Gvsp("127.0.0.1")
        try:
            gev.start_stream(self.cam, rx, "127.0.0.1")
            self.assertStatus(gev.BUSY, self.cam.write_reg, gev.REG["WIDTH"], 320)
        finally:
            gev.stop_stream(self.cam)
            rx.close()

    def test_frame_rate_paces_stream(self):
        self.cam.write_float(gev.REG["FRAME_RATE"], 20.0)
        frames = self.grab(4)
        gaps = [(b["timestamp"] - a["timestamp"]) / 1e6 for a, b in zip(frames, frames[1:])]
        for g in gaps:
            self.assertAlmostEqual(g, 50.0, delta=10.0)

    def test_fire_test_packet(self):
        rx = gev.Gvsp("127.0.0.1")
        try:
            self.cam.write_reg(gev.BS_SCDA0, struct.unpack(">I", bytes([127, 0, 0, 1]))[0])
            self.cam.write_reg(gev.BS_SCP0, rx.port)
            self.cam.write_reg(gev.BS_SCPS0, 0x80000000 | 2000)
            self.assertEqual(len(rx.recv_raw()), 2000 - 28)
        finally:
            self.cam.write_reg(gev.BS_SCP0, 0)
            rx.close()


if __name__ == "__main__":
    unittest.main(verbosity=2)

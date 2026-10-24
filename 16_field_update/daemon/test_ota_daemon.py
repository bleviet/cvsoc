#!/usr/bin/env python3
"""
test_ota_daemon.py — End-to-end unit and fault-injection tests for
16_field_update (Part 1 happy-path OTA update + Part 3 automatic rollback).

Compiles the real C `led_server` variants (v1.0.0, v2.0.0, and crashing
v2.0.0-broken) for the host CPU using `--mock-mem`, spins up `ota_daemon` on an
ephemeral HTTP port, and verifies:
  1. Initial boot into slot_a (v1.0.0, LED pattern 0x0F)
  2. Live network update to slot_b (v2.0.0, LED pattern 0xA5)
  3. Fault A: Corrupted network payload (SHA-256 mismatch -> rejected, zero downtime)
  4. Fault B: Bad/uncompressed FPGA bitstream (CONF_DONE fail -> auto-rollback)
  5. Fault C: Crashing application binary (UDP health probe fail -> auto-rollback)
  6. Fault D: Power-cut mid-update boot guard recovery
"""

import json
import os
import shutil
import socket
import subprocess
import sys
import tempfile
import threading
import time
import unittest

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
PROJECT_DIR = os.path.dirname(SCRIPT_DIR)
sys.path.insert(0, SCRIPT_DIR)
sys.path.insert(0, os.path.join(PROJECT_DIR, "client"))

import ota_client  # noqa: E402
import ota_daemon  # noqa: E402
import pack_bundle  # noqa: E402


def find_free_port(sock_type: int = socket.SOCK_STREAM) -> int:
    with socket.socket(socket.AF_INET, sock_type) as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


class LocalProcessBackend:
    """
    Test backend that runs the real compiled `led_server` C binary on a local
    UDP port (with `--mock-mem`) and simulates FPGA Manager `.rbf` loading.
    """

    def __init__(self, fw_root: str, udp_port: int, mock_mem_path: str):
        self.fw_root = fw_root
        self.udp_port = udp_port
        self.mock_mem_path = mock_mem_path
        self.proc: subprocess.Popen | None = None
        self.loaded_rbf_slot: str | None = None

    def stop_app(self) -> tuple[bool, str]:
        if self.proc is not None:
            if self.proc.poll() is None:
                self.proc.terminate()
                try:
                    self.proc.wait(timeout=3)
                except subprocess.TimeoutExpired:
                    self.proc.kill()
                    self.proc.wait(timeout=1)
            if self.proc.stdout:
                self.proc.stdout.close()
            if self.proc.stderr:
                self.proc.stderr.close()
            self.proc = None
        return True, "local led_server stopped"

    def load_fpga(self, slot_dir: str) -> tuple[bool, str]:
        rbf_path = os.path.join(slot_dir, "de10_nano.rbf")
        if not os.path.isfile(rbf_path):
            return False, f"missing bitstream: {rbf_path}"
        with open(rbf_path, "rb") as f:
            data = f.read(64)
        if data.startswith(b"UNCOMPRESSED_INVALID_RBF_HEADER"):
            return (
                False,
                "fpga_load: TIMEOUT - CONF_DONE never asserted (MSEL=0x0A rejected uncompressed RBF)",
            )
        self.loaded_rbf_slot = os.path.basename(slot_dir)
        return True, f"fpga_load: CONF_DONE + USER_MODE OK ({self.loaded_rbf_slot})"

    def start_app(self, slot_dir: str) -> tuple[bool, str]:
        self.stop_app()
        bin_path = os.path.join(slot_dir, "led_server")
        if not os.path.isfile(bin_path):
            return False, f"missing binary: {bin_path}"
        self.proc = subprocess.Popen(
            [
                bin_path,
                "--port",
                str(self.udp_port),
                "--mock-mem",
                self.mock_mem_path,
            ],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )
        time.sleep(0.15)
        if self.proc.poll() is not None:
            _, stderr = self.proc.communicate(timeout=1)
            err_msg = stderr.decode("utf-8", errors="replace").strip()
            return False, f"led_server exited immediately (code {self.proc.returncode}): {err_msg}"
        return True, f"led_server started (PID {self.proc.pid})"

    def health_check(self) -> tuple[bool, str, int | None]:
        return ota_daemon.udp_health_probe(
            host="127.0.0.1", port=self.udp_port, timeout=1.0, retries=4, retry_delay=0.1
        )


class TestOTADaemonEndToEnd(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.build_dir = tempfile.mkdtemp(prefix="cvsoc_ota_bins_")
        led_src = os.path.join(PROJECT_DIR, "software", "led_server", "led_server.c")

        cls.bin_v1 = os.path.join(cls.build_dir, "led_server_v1")
        cls.bin_v2 = os.path.join(cls.build_dir, "led_server_v2")
        cls.bin_crash = os.path.join(cls.build_dir, "led_server_crash")

        subprocess.check_call(
            [
                "gcc", "-O2", "-Wall", "-Wextra",
                '-DAPP_VERSION_STR="1.0.0"', "-DDEFAULT_STARTUP_PATTERN=0x0F",
                "-o", cls.bin_v1, led_src,
            ]
        )
        subprocess.check_call(
            [
                "gcc", "-O2", "-Wall", "-Wextra",
                '-DAPP_VERSION_STR="2.0.0"', "-DDEFAULT_STARTUP_PATTERN=0xA5",
                "-o", cls.bin_v2, led_src,
            ]
        )
        subprocess.check_call(
            [
                "gcc", "-O2", "-Wall", "-Wextra",
                '-DAPP_VERSION_STR="2.0.0-broken"', "-DSIMULATE_CRASH_ON_START=1",
                "-o", cls.bin_crash, led_src,
            ]
        )

    @classmethod
    def tearDownClass(cls) -> None:
        shutil.rmtree(cls.build_dir, ignore_errors=True)

    def setUp(self) -> None:
        self.tmp_dir = tempfile.mkdtemp(prefix="cvsoc_ota_test_")
        self.fw_root = os.path.join(self.tmp_dir, "firmware")
        self.mock_mem = os.path.join(self.tmp_dir, "mock_lwh2f.bin")
        self.udp_port = find_free_port(socket.SOCK_DGRAM)

        self.rbf_v1 = os.path.join(self.tmp_dir, "v1.rbf")
        self.rbf_v2 = os.path.join(self.tmp_dir, "v2.rbf")
        with open(self.rbf_v1, "wb") as f:
            f.write(b"COMPRESSED_CYCLONEV_RBF_V1\x00" * 64)
        with open(self.rbf_v2, "wb") as f:
            f.write(b"COMPRESSED_CYCLONEV_RBF_V2\x00" * 64)

        self.backend = LocalProcessBackend(self.fw_root, self.udp_port, self.mock_mem)
        self.mgr = ota_daemon.SlotManager(fw_root=self.fw_root, backend=self.backend)

        # Populate Golden v1.0.0 in slot_a and start it
        slot_a_dir = self.mgr.slot_dir("slot_a")
        shutil.copy2(self.rbf_v1, os.path.join(slot_a_dir, "de10_nano.rbf"))
        shutil.copy2(self.bin_v1, os.path.join(slot_a_dir, "led_server"))
        os.chmod(os.path.join(slot_a_dir, "led_server"), 0o755)
        with open(os.path.join(slot_a_dir, "manifest.json"), "w", encoding="utf-8") as f:
            json.dump(
                {
                    "schema_version": 1,
                    "version": "1.0.0",
                    "description": "Factory Golden v1.0.0",
                    "files": {
                        "de10_nano.rbf": {
                            "sha256": ota_daemon.sha256_file(os.path.join(slot_a_dir, "de10_nano.rbf")),
                            "size": os.path.getsize(os.path.join(slot_a_dir, "de10_nano.rbf")),
                        },
                        "led_server": {
                            "sha256": ota_daemon.sha256_file(os.path.join(slot_a_dir, "led_server")),
                            "size": os.path.getsize(os.path.join(slot_a_dir, "led_server")),
                        },
                    },
                },
                f,
            )

        self.backend.load_fpga(slot_a_dir)
        self.backend.start_app(slot_a_dir)

        handler_cls = ota_daemon.make_handler(self.mgr)
        self.http_server = ota_daemon.http.server.HTTPServer(
            ("127.0.0.1", 0), handler_cls
        )
        self.http_port = self.http_server.server_port
        self.server_thread = threading.Thread(
            target=self.http_server.serve_forever, kwargs={"poll_interval": 0.05}, daemon=True
        )
        self.server_thread.start()
        self.base_url = f"http://127.0.0.1:{self.http_port}"

    def tearDown(self) -> None:
        self.http_server.shutdown()
        self.http_server.server_close()
        self.backend.stop_app()
        shutil.rmtree(self.tmp_dir, ignore_errors=True)

    def _make_bundle(self, name: str, rbf_path: str, app_path: str,
                     version: str, corrupt_payload: bool = False,
                     bad_rbf: bool = False) -> str:
        out_path = os.path.join(self.tmp_dir, name)
        with open(rbf_path, "rb") as f:
            rbf_bytes = f.read()
        with open(app_path, "rb") as f:
            app_bytes = f.read()
        pack_bundle.create_bundle(
            rbf_bytes=rbf_bytes,
            app_bytes=app_bytes,
            version=version,
            description=f"Test bundle {name}",
            output_path=out_path,
            corrupt_payload=corrupt_payload,
            bad_rbf=bad_rbf,
        )
        return out_path

    def test_01_part1_happy_path_live_update(self) -> None:
        """Part 1: Verify power-up state (v1.0.0, LED=0x0F) and live OTA update to v2.0.0 (LED=0xA5)."""
        code, st = ota_client.http_json_request(f"{self.base_url}/status")
        self.assertEqual(code, 200)
        self.assertEqual(st["active_slot"], "slot_a")
        self.assertEqual(st["active_version"], "1.0.0")

        ok, _, pattern = self.backend.health_check()
        self.assertTrue(ok)
        self.assertEqual(pattern, 0x0F)

        bundle_v2 = self._make_bundle("fw_v2.tar.gz", self.rbf_v2, self.bin_v2, "2.0.0")
        with open(bundle_v2, "rb") as f:
            code, res = ota_client.http_json_request(
                f"{self.base_url}/update", method="POST", data=f.read()
            )

        self.assertEqual(code, 200)
        self.assertTrue(res["ok"])
        self.assertEqual(res["status"], "committed")
        self.assertEqual(res["active_slot"], "slot_b")
        self.assertEqual(res["active_version"], "2.0.0")
        self.assertEqual(res["led_pattern"], "0xA5")
        self.assertEqual(os.readlink(self.mgr.active_link), "slot_b")
        self.assertEqual(self.backend.loaded_rbf_slot, "slot_b")

    def test_02_part3_fault_a_corrupted_transfer_rejected(self) -> None:
        """Task 3.1: Corrupted network payload fails SHA-256 check with zero downtime."""
        pid_before = self.backend.proc.pid if self.backend.proc else None
        bundle_corrupt = self._make_bundle(
            "fw_corrupt.tar.gz", self.rbf_v2, self.bin_v2, "2.0.0", corrupt_payload=True
        )
        with open(bundle_corrupt, "rb") as f:
            code, res = ota_client.http_json_request(
                f"{self.base_url}/update", method="POST", data=f.read()
            )

        self.assertEqual(code, 400)
        self.assertFalse(res["ok"])
        self.assertEqual(res["status"], "rejected")
        self.assertEqual(res["failed_stage"], "checksum_verification")
        self.assertEqual(res["active_slot"], "slot_a")
        # Running v1.0.0 process was never even stopped
        self.assertEqual(self.backend.proc.pid if self.backend.proc else None, pid_before)

    def test_03_part3_fault_b_bad_bitstream_rolls_back(self) -> None:
        """Task 3.2: Invalid/uncompressed RBF fails FPGA programming and automatically rolls back to slot_a."""
        bundle_bad_rbf = self._make_bundle(
            "fw_bad_rbf.tar.gz", self.rbf_v2, self.bin_v2, "2.0.0", bad_rbf=True
        )
        with open(bundle_bad_rbf, "rb") as f:
            code, res = ota_client.http_json_request(
                f"{self.base_url}/update", method="POST", data=f.read()
            )

        self.assertEqual(code, 422)
        self.assertFalse(res["ok"])
        self.assertEqual(res["status"], "rolled_back")
        self.assertEqual(res["failed_stage"], "fpga_programming")
        self.assertEqual(res["active_slot"], "slot_a")
        self.assertEqual(res["active_version"], "1.0.0")
        self.assertEqual(res["led_pattern"], "0x0F")
        self.assertEqual(self.backend.loaded_rbf_slot, "slot_a")

        ok, _, pattern = self.backend.health_check()
        self.assertTrue(ok)
        self.assertEqual(pattern, 0x0F)

    def test_04_part3_fault_c_crashing_app_rolls_back(self) -> None:
        """Task 3.3: Defective led_server binary crashes on start and triggers automatic rollback to slot_a."""
        bundle_crash = self._make_bundle(
            "fw_crash.tar.gz", self.rbf_v2, self.bin_crash, "2.0.0-broken"
        )
        with open(bundle_crash, "rb") as f:
            code, res = ota_client.http_json_request(
                f"{self.base_url}/update", method="POST", data=f.read()
            )

        self.assertEqual(code, 422)
        self.assertFalse(res["ok"])
        self.assertEqual(res["status"], "rolled_back")
        self.assertIn(res["failed_stage"], ("app_start", "app_health_check"))
        self.assertEqual(res["active_slot"], "slot_a")
        self.assertEqual(res["active_version"], "1.0.0")
        self.assertEqual(res["led_pattern"], "0x0F")
        self.assertEqual(os.readlink(self.mgr.active_link), "slot_a")
        self.assertEqual(self.backend.loaded_rbf_slot, "slot_a")

        ok, _, pattern = self.backend.health_check()
        self.assertTrue(ok)
        self.assertEqual(pattern, 0x0F)

    def test_05_part3_boot_guard_power_loss_recovery(self) -> None:
        """Task 3.4: Simulate power cut while pending_slot='slot_b' is uncommitted; boot guard restores slot_a."""
        state = self.mgr.load_state()
        state["active_slot"] = "slot_a"
        state["pending_slot"] = "slot_b"
        self.mgr.save_state(state)
        ota_daemon.atomic_symlink("slot_b", self.mgr.active_link)

        code, rec = ota_client.http_json_request(f"{self.base_url}/recover", method="POST")
        self.assertEqual(code, 200)
        self.assertTrue(rec["recovered"])
        self.assertEqual(rec["active_slot"], "slot_a")
        self.assertEqual(os.readlink(self.mgr.active_link), "slot_a")

        st = self.mgr.get_status()
        self.assertIsNone(st["pending_slot"])
        self.assertEqual(st["last_rollback"]["stage"], "boot_guard")


if __name__ == "__main__":
    unittest.main(verbosity=2)

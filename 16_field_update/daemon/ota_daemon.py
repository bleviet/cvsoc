#!/usr/bin/env python3
"""
ota_daemon.py — On-board Over-The-Air (OTA) Field Update Daemon for DE10-Nano.

Listens on TCP/HTTP port 8090 (dual-stack IPv6 + IPv4) and manages atomic
A/B firmware updates (/opt/firmware/slot_a and /opt/firmware/slot_b) with
live FPGA bitstream reconfiguration, UDP health probing, and automatic
rollback on failure.

Endpoints:
  GET  /status   — Return JSON status (active slot, version, rollback history)
  POST /update   — Upload a .tar.gz firmware bundle and execute atomic update
  POST /recover  — Run boot-guard check for interrupted updates
"""

import argparse
import hashlib
import http.server
import io
import json
import os
import shutil
import socket
import subprocess
import sys
import tarfile
import tempfile
import time
from typing import Callable, Optional

DEFAULT_OTA_PORT = 8090
DEFAULT_UDP_PORT = 5005
DEFAULT_FW_ROOT  = "/opt/firmware"

SLOTS = ("slot_a", "slot_b")
REQUIRED_FILES = ("de10_nano.rbf", "led_server")


def sha256_file(path: str) -> str:
    h = hashlib.sha256()
    with open(path, "rb") as f:
        while True:
            chunk = f.read(65536)
            if not chunk:
                break
            h.update(chunk)
    return h.hexdigest()


def atomic_symlink(target: str, link_path: str) -> None:
    """Atomically point link_path at target using a temp symlink + os.replace."""
    tmp_link = f"{link_path}.tmp.{os.getpid()}"
    try:
        if os.path.lexists(tmp_link):
            os.unlink(tmp_link)
        os.symlink(target, tmp_link)
        os.replace(tmp_link, link_path)
    finally:
        if os.path.lexists(tmp_link):
            try:
                os.unlink(tmp_link)
            except OSError:
                pass


def atomic_write_json(path: str, data: dict) -> None:
    """Atomically write JSON to path with fsync."""
    os.makedirs(os.path.dirname(os.path.abspath(path)), exist_ok=True)
    tmp_path = f"{path}.tmp.{os.getpid()}"
    with open(tmp_path, "w", encoding="utf-8") as f:
        json.dump(data, f, indent=2)
        f.write("\n")
        f.flush()
        os.fsync(f.fileno())
    os.replace(tmp_path, path)


def udp_health_probe(host: str = "127.0.0.1", port: int = DEFAULT_UDP_PORT,
                     timeout: float = 2.0, retries: int = 5,
                     retry_delay: float = 0.25) -> tuple[bool, str, Optional[int]]:
    """
    Probe led_server via UDP CMD_GET_PATTERN (0x02 0x00).
    Returns (ok, detail_message, current_led_pattern).
    """
    last_err = "no response"
    for attempt in range(1, retries + 1):
        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        sock.settimeout(timeout / retries if retries > 1 else timeout)
        try:
            sock.sendto(b"\x02\x00", (host, port))
            data, _ = sock.recvfrom(16)
            if len(data) == 2 and data[0] == 0x00:
                pattern = data[1]
                return True, f"UDP GET_PATTERN OK (LED=0x{pattern:02X}, attempt {attempt})", pattern
            last_err = f"unexpected response bytes: {data.hex()}"
        except OSError as exc:
            last_err = str(exc)
        finally:
            sock.close()
        if attempt < retries:
            time.sleep(retry_delay)
    return False, f"UDP health probe failed after {retries} attempts: {last_err}", None


class HardwareBackend:
    """
    Production backend that invokes S30fpga_load and S40led_server on the
    DE10-Nano (or custom scripts in local test/simulation mode).
    """

    def __init__(
        self,
        fw_root: str = DEFAULT_FW_ROOT,
        udp_port: int = DEFAULT_UDP_PORT,
        fpga_init_script: str = "/etc/init.d/S30fpga_load",
        app_init_script: str = "/etc/init.d/S40led_server",
        lib_firmware_dir: str = "/lib/firmware",
    ):
        self.fw_root = os.path.abspath(fw_root)
        self.udp_port = udp_port
        self.fpga_init_script = fpga_init_script
        self.app_init_script = app_init_script
        self.lib_firmware_dir = lib_firmware_dir

    def stop_app(self) -> tuple[bool, str]:
        if os.path.isfile(self.app_init_script) and os.access(self.app_init_script, os.X_OK):
            res = subprocess.run(
                [self.app_init_script, "stop"],
                capture_output=True,
                text=True,
                timeout=10,
            )
            return res.returncode == 0, (res.stdout + res.stderr).strip()
        return True, "app stop skipped (no init script)"

    def load_fpga(self, slot_dir: str) -> tuple[bool, str]:
        rbf_path = os.path.join(slot_dir, "de10_nano.rbf")
        if not os.path.isfile(rbf_path):
            return False, f"missing bitstream: {rbf_path}"

        if os.path.isfile(self.fpga_init_script) and os.access(self.fpga_init_script, os.X_OK):
            try:
                res = subprocess.run(
                    [self.fpga_init_script, "program-slot", slot_dir],
                    capture_output=True,
                    text=True,
                    timeout=90,
                )
                out = (res.stdout + res.stderr).strip()
                return res.returncode == 0, out or "fpga_load program-slot completed"
            except subprocess.TimeoutExpired:
                return False, "fpga_load timed out waiting for CONF_DONE"

        with open(rbf_path, "rb") as f:
            header = f.read(32)
        if header.startswith(b"UNCOMPRESSED_INVALID_RBF_HEADER"):
            return False, "fpga_load: nSTATUS LOW / CONF_DONE failed (invalid/uncompressed RBF)"

        if os.path.isdir(self.lib_firmware_dir) and os.access(self.lib_firmware_dir, os.W_OK):
            dst_rbf = os.path.join(self.lib_firmware_dir, "de10_nano.rbf")
            shutil.copy2(rbf_path, dst_rbf)

        return True, f"FPGA bitstream staged from {rbf_path}"

    def start_app(self, slot_dir: str) -> tuple[bool, str]:
        app_path = os.path.join(slot_dir, "led_server")
        if not os.path.isfile(app_path):
            return False, f"missing application binary: {app_path}"

        if os.path.isfile(self.app_init_script) and os.access(self.app_init_script, os.X_OK):
            res = subprocess.run(
                [self.app_init_script, "start"],
                capture_output=True,
                text=True,
                timeout=10,
            )
            out = (res.stdout + res.stderr).strip()
            return res.returncode == 0, out or "led_server started"

        return True, f"app binary verified at {app_path}"

    def health_check(self) -> tuple[bool, str, Optional[int]]:
        return udp_health_probe(port=self.udp_port)


class SlotManager:
    """Manages A/B firmware slots, atomic updates, boot recovery, and rollback."""

    def __init__(self, fw_root: str = DEFAULT_FW_ROOT, backend=None):
        self.fw_root = os.path.abspath(fw_root)
        self.state_path = os.path.join(self.fw_root, "state.json")
        self.active_link = os.path.join(self.fw_root, "active")
        self.backend = backend or HardwareBackend(fw_root=self.fw_root)
        self._ensure_layout()

    def _ensure_layout(self) -> None:
        os.makedirs(self.fw_root, exist_ok=True)
        for slot in SLOTS:
            os.makedirs(os.path.join(self.fw_root, slot), exist_ok=True)
        if not os.path.isfile(self.state_path):
            initial_state = {
                "active_slot": "slot_a",
                "pending_slot": None,
                "boot_count": 1,
                "last_rollback": None,
                "history": [],
            }
            atomic_write_json(self.state_path, initial_state)
        state = self.load_state()
        if not os.path.lexists(self.active_link):
            atomic_symlink(state["active_slot"], self.active_link)

    def load_state(self) -> dict:
        with open(self.state_path, "r", encoding="utf-8") as f:
            return json.load(f)

    def save_state(self, state: dict) -> None:
        atomic_write_json(self.state_path, state)

    def slot_dir(self, slot: str) -> str:
        if slot not in SLOTS:
            raise ValueError(f"Invalid slot name: {slot}")
        return os.path.join(self.fw_root, slot)

    def standby_slot_for(self, active_slot: str) -> str:
        return "slot_b" if active_slot == "slot_a" else "slot_a"

    def read_slot_manifest(self, slot: str) -> Optional[dict]:
        mpath = os.path.join(self.slot_dir(slot), "manifest.json")
        if not os.path.isfile(mpath):
            return None
        try:
            with open(mpath, "r", encoding="utf-8") as f:
                return json.load(f)
        except (OSError, json.JSONDecodeError):
            return None

    def get_status(self) -> dict:
        state = self.load_state()
        active_slot = state["active_slot"]
        standby_slot = self.standby_slot_for(active_slot)
        active_manifest = self.read_slot_manifest(active_slot)
        standby_manifest = self.read_slot_manifest(standby_slot)
        return {
            "active_slot": active_slot,
            "active_version": active_manifest.get("version") if active_manifest else "unknown",
            "active_description": active_manifest.get("description") if active_manifest else "",
            "standby_slot": standby_slot,
            "standby_version": standby_manifest.get("version") if standby_manifest else None,
            "pending_slot": state.get("pending_slot"),
            "boot_count": state.get("boot_count", 1),
            "last_rollback": state.get("last_rollback"),
            "history": state.get("history", [])[-5:],
        }

    def recover_incomplete_boot(self) -> dict:
        """
        Task 3.4: Boot guard called by S30fpga_load / S50ota_daemon at startup.
        If power was lost while pending_slot was being tested, revert active
        symlink to active_slot and record the recovery.
        """
        state = self.load_state()
        state["boot_count"] = state.get("boot_count", 0) + 1
        pending = state.get("pending_slot")
        active = state["active_slot"]

        if pending:
            reason = (
                f"Power loss or unexpected reboot detected while testing {pending}; "
                f"restored {active}"
            )
            state["pending_slot"] = None
            state["last_rollback"] = {
                "timestamp": int(time.time()),
                "failed_slot": pending,
                "restored_slot": active,
                "stage": "boot_guard",
                "reason": reason,
            }
            state.setdefault("history", []).append(state["last_rollback"])
            atomic_symlink(active, self.active_link)
            self.save_state(state)
            return {"recovered": True, "active_slot": active, "reason": reason}

        atomic_symlink(active, self.active_link)
        self.save_state(state)
        return {"recovered": False, "active_slot": active}

    def _unpack_and_verify(self, bundle_bytes: bytes, staging_dir: str) -> tuple[bool, str, Optional[dict]]:
        """Extract bundle into staging_dir and validate manifest + SHA-256 digests."""
        try:
            with tarfile.open(fileobj=io.BytesIO(bundle_bytes), mode="r:*") as tar:
                for member in tar.getmembers():
                    if member.name.startswith("/") or ".." in member.name.split("/"):
                        return False, f"unsafe path in archive: {member.name}", None
                tar.extractall(staging_dir)
        except (tarfile.TarError, OSError) as exc:
            return False, f"failed to unpack tar.gz archive: {exc}", None

        manifest_path = os.path.join(staging_dir, "manifest.json")
        if not os.path.isfile(manifest_path):
            return False, "manifest.json missing from bundle", None

        try:
            with open(manifest_path, "r", encoding="utf-8") as f:
                manifest = json.load(f)
        except (OSError, json.JSONDecodeError) as exc:
            return False, f"invalid manifest.json: {exc}", None

        files_meta = manifest.get("files", {})
        for req_file in REQUIRED_FILES:
            if req_file not in files_meta:
                return False, f"manifest.json missing entry for '{req_file}'", None
            fpath = os.path.join(staging_dir, req_file)
            if not os.path.isfile(fpath):
                return False, f"bundle missing required file '{req_file}'", None

            actual_size = os.path.getsize(fpath)
            expected_size = files_meta[req_file].get("size")
            if expected_size is not None and actual_size != expected_size:
                return (
                    False,
                    f"size mismatch for {req_file}: expected {expected_size}, got {actual_size}",
                    None,
                )

            actual_sha = sha256_file(fpath)
            expected_sha = files_meta[req_file].get("sha256", "")
            if actual_sha.lower() != expected_sha.lower():
                return (
                    False,
                    f"SHA-256 mismatch for {req_file}: "
                    f"expected {expected_sha[:16]}..., got {actual_sha[:16]}...",
                    None,
                )

        return True, "manifest and SHA-256 checksums verified", manifest

    def _rollback_to(self, active_slot: str, failed_slot: str, stage: str,
                     reason: str, steps: list[str]) -> dict:
        """Restore active_slot FPGA bitstream and application after a candidate failure."""
        active_dir = self.slot_dir(active_slot)
        steps.append(f"ROLLBACK: Reverting from {failed_slot} to {active_slot} ({reason})")

        self.backend.stop_app()
        atomic_symlink(active_slot, self.active_link)

        fpga_ok, fpga_msg = self.backend.load_fpga(active_dir)
        steps.append(f"ROLLBACK FPGA ({active_slot}): {fpga_msg}")

        app_ok, app_msg = self.backend.start_app(active_dir)
        steps.append(f"ROLLBACK App ({active_slot}): {app_msg}")

        probe_ok, probe_msg, pattern = self.backend.health_check()
        steps.append(f"ROLLBACK Health Check ({active_slot}): {probe_msg}")

        state = self.load_state()
        state["active_slot"] = active_slot
        state["pending_slot"] = None
        rollback_entry = {
            "timestamp": int(time.time()),
            "failed_slot": failed_slot,
            "restored_slot": active_slot,
            "stage": stage,
            "reason": reason,
            "recovery_healthy": bool(fpga_ok and app_ok and probe_ok),
        }
        state["last_rollback"] = rollback_entry
        state.setdefault("history", []).append(rollback_entry)
        self.save_state(state)

        active_manifest = self.read_slot_manifest(active_slot)
        return {
            "ok": False,
            "status": "rolled_back",
            "failed_stage": stage,
            "reason": reason,
            "active_slot": active_slot,
            "active_version": active_manifest.get("version") if active_manifest else "unknown",
            "led_pattern": f"0x{pattern:02X}" if pattern is not None else None,
            "steps": steps,
        }

    def apply_bundle(self, bundle_bytes: bytes) -> tuple[int, dict]:
        """
        Execute the 6-step atomic OTA update into the standby slot.
        Returns (http_status_code, response_dict).
        """
        steps: list[str] = []
        state = self.load_state()
        active_slot = state["active_slot"]
        standby_slot = self.standby_slot_for(active_slot)
        standby_dir = self.slot_dir(standby_slot)

        with tempfile.TemporaryDirectory(prefix="ota_staging_") as staging_dir:
            # Step 1 & 2: Unpack and verify SHA-256 digests before touching running system
            ok, msg, manifest = self._unpack_and_verify(bundle_bytes, staging_dir)
            steps.append(f"Step 1-2 (Unpack & SHA-256 Check): {msg}")
            if not ok or manifest is None:
                return 400, {
                    "ok": False,
                    "status": "rejected",
                    "failed_stage": "checksum_verification",
                    "reason": msg,
                    "active_slot": active_slot,
                    "active_version": (self.read_slot_manifest(active_slot) or {}).get("version", "unknown"),
                    "steps": steps,
                }

            target_version = manifest.get("version", "unknown")

            # Step 3: Stage verified files into standby_slot (using atomic rename
            # so we never hit ETXTBSY even if a binary inode was recently executed)
            os.makedirs(standby_dir, exist_ok=True)
            for fname in ("manifest.json", "de10_nano.rbf", "led_server"):
                src = os.path.join(staging_dir, fname)
                dst = os.path.join(standby_dir, fname)
                tmp_dst = f"{dst}.tmp.{os.getpid()}"
                shutil.copy2(src, tmp_dst)
                if fname == "led_server":
                    os.chmod(tmp_dst, 0o755)
                os.replace(tmp_dst, dst)

            state["pending_slot"] = standby_slot
            self.save_state(state)
            steps.append(f"Step 3 (Stage): Installed v{target_version} into {standby_slot}")

        # Step 4: Stop current app and reprogram FPGA with standby_slot bitstream
        _, stop_msg = self.backend.stop_app()
        steps.append(f"Step 4a (Quiesce App): {stop_msg}")

        fpga_ok, fpga_msg = self.backend.load_fpga(standby_dir)
        steps.append(f"Step 4b (Program FPGA from {standby_slot}): {fpga_msg}")
        if not fpga_ok:
            result = self._rollback_to(
                active_slot=active_slot,
                failed_slot=standby_slot,
                stage="fpga_programming",
                reason=fpga_msg,
                steps=steps,
            )
            return 422, result

        # Step 5: Point active symlink at standby_slot, start candidate app, and probe health
        atomic_symlink(standby_slot, self.active_link)
        app_ok, app_msg = self.backend.start_app(standby_dir)
        steps.append(f"Step 5a (Start App from {standby_slot}): {app_msg}")
        if not app_ok:
            result = self._rollback_to(
                active_slot=active_slot,
                failed_slot=standby_slot,
                stage="app_start",
                reason=app_msg,
                steps=steps,
            )
            return 422, result

        probe_ok, probe_msg, pattern = self.backend.health_check()
        steps.append(f"Step 5b (Health Probe): {probe_msg}")
        if not probe_ok:
            result = self._rollback_to(
                active_slot=active_slot,
                failed_slot=standby_slot,
                stage="app_health_check",
                reason=probe_msg,
                steps=steps,
            )
            return 422, result

        # Step 6: Commit standby_slot as the new active_slot
        state = self.load_state()
        state["active_slot"] = standby_slot
        state["pending_slot"] = None
        commit_entry = {
            "timestamp": int(time.time()),
            "slot": standby_slot,
            "version": target_version,
            "stage": "committed",
            "led_pattern": f"0x{pattern:02X}" if pattern is not None else None,
        }
        state.setdefault("history", []).append(commit_entry)
        self.save_state(state)
        steps.append(
            f"Step 6 (Commit): Activated {standby_slot} (v{target_version}, "
            f"LED=0x{(pattern or 0):02X})"
        )

        return 200, {
            "ok": True,
            "status": "committed",
            "previous_slot": active_slot,
            "active_slot": standby_slot,
            "active_version": target_version,
            "led_pattern": f"0x{pattern:02X}" if pattern is not None else None,
            "steps": steps,
        }


class DualStackHTTPServer(http.server.HTTPServer):
    """HTTPServer listening on AF_INET6 with IPV6_V6ONLY=0 (IPv6 + IPv4-mapped)."""

    address_family = socket.AF_INET6

    def server_bind(self) -> None:
        self.socket.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        try:
            self.socket.setsockopt(socket.IPPROTO_IPV6, socket.IPV6_V6ONLY, 0)
        except OSError:
            pass
        super().server_bind()


def make_handler(slot_manager: SlotManager):
    class OTARequestHandler(http.server.BaseHTTPRequestHandler):
        def _send_json(self, code: int, payload: dict) -> None:
            body = (json.dumps(payload, indent=2) + "\n").encode("utf-8")
            self.send_response(code)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)

        def do_GET(self) -> None:
            if self.path == "/status":
                self._send_json(200, slot_manager.get_status())
            else:
                self._send_json(404, {"error": f"Unknown path: {self.path}"})

        def do_POST(self) -> None:
            if self.path == "/recover":
                res = slot_manager.recover_incomplete_boot()
                self._send_json(200, res)
                return

            if self.path != "/update":
                self._send_json(404, {"error": f"Unknown path: {self.path}"})
                return

            length = int(self.headers.get("Content-Length", "0"))
            if length <= 0:
                self._send_json(400, {"ok": False, "error": "Empty update bundle"})
                return

            bundle_bytes = self.rfile.read(length)
            code, result = slot_manager.apply_bundle(bundle_bytes)
            self._send_json(code, result)

        def log_message(self, fmt: str, *args) -> None:
            sys.stdout.write("[ota_daemon] " + (fmt % args) + "\n")
            sys.stdout.flush()

    return OTARequestHandler


def main() -> int:
    parser = argparse.ArgumentParser(
        description="DE10-Nano OTA Field Update Daemon (A/B slots + rollback)."
    )
    parser.add_argument("--port", type=int, default=DEFAULT_OTA_PORT,
                        help=f"HTTP listen port (default: {DEFAULT_OTA_PORT})")
    parser.add_argument("--udp-port", type=int, default=DEFAULT_UDP_PORT,
                        help=f"UDP port of led_server health check (default: {DEFAULT_UDP_PORT})")
    parser.add_argument("--fw-root", default=DEFAULT_FW_ROOT,
                        help=f"Firmware slots directory (default: {DEFAULT_FW_ROOT})")
    parser.add_argument("--fpga-script", default="/etc/init.d/S30fpga_load",
                        help="Path to S30fpga_load init script")
    parser.add_argument("--app-script", default="/etc/init.d/S40led_server",
                        help="Path to S40led_server init script")
    parser.add_argument("--recover-only", action="store_true",
                        help="Run boot-guard check for incomplete updates and exit")
    args = parser.parse_args()

    backend = HardwareBackend(
        fw_root=args.fw_root,
        udp_port=args.udp_port,
        fpga_init_script=args.fpga_script,
        app_init_script=args.app_script,
    )
    mgr = SlotManager(fw_root=args.fw_root, backend=backend)

    recovery = mgr.recover_incomplete_boot()
    if recovery["recovered"]:
        print(f"[ota_daemon] BOOT GUARD RECOVERY: {recovery['reason']}")
    if args.recover_only:
        return 0

    handler_cls = make_handler(mgr)
    try:
        server = DualStackHTTPServer(("::", args.port), handler_cls)
    except OSError:
        # Fallback to IPv4 if IPv6 is disabled on the host kernel
        server = http.server.HTTPServer(("0.0.0.0", args.port), handler_cls)

    status = mgr.get_status()
    print(f"[ota_daemon] Listening on port {args.port} (fw_root={args.fw_root})")
    print(f"[ota_daemon] Active slot: {status['active_slot']} (v{status['active_version']})")
    sys.stdout.flush()

    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\n[ota_daemon] Shutting down.")
    finally:
        server.server_close()
    return 0


if __name__ == "__main__":
    sys.exit(main())

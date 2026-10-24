#!/usr/bin/env python3
"""
ota_client.py — PC-side CLI client for the DE10-Nano OTA Field Update Daemon.

Supports IPv4, global IPv6, and link-local IPv6 (with %iface zone IDs).

Usage:
  python3 ota_client.py --host 192.168.1.100 status
  python3 ota_client.py --host 192.168.1.100 push build/fw_v2.0.0.tar.gz
  python3 ota_client.py --host fe80::2833:8aff:fe95:cb3d%eth0 push build/fw_v2.0.0.tar.gz
"""

import argparse
import json
import sys
import urllib.error
import urllib.request


DEFAULT_OTA_PORT = 8090


def format_base_url(host: str, port: int) -> str:
    """Format host and port into an HTTP base URL (handling IPv6 & %zone_id)."""
    clean = host.strip("[]")
    if ":" in clean:
        # IPv6 literal — RFC 6874 percent-encodes the % zone separator as %25
        if "%" in clean and "%25" not in clean:
            addr, zone = clean.split("%", 1)
            clean = f"{addr}%25{zone}"
        return f"http://[{clean}]:{port}"
    return f"http://{clean}:{port}"


def http_json_request(url: str, method: str = "GET", data: bytes | None = None,
                      timeout: float = 95.0) -> tuple[int, dict]:
    headers = {}
    if data is not None:
        headers["Content-Type"] = "application/gzip"
        headers["Content-Length"] = str(len(data))
    req = urllib.request.Request(url, data=data, headers=headers, method=method)
    try:
        with urllib.request.urlopen(req, timeout=timeout) as resp:
            body = resp.read().decode("utf-8")
            return resp.status, json.loads(body)
    except urllib.error.HTTPError as exc:
        body = exc.read().decode("utf-8", errors="replace")
        try:
            return exc.code, json.loads(body)
        except json.JSONDecodeError:
            return exc.code, {"ok": False, "error": body}


def print_status(payload: dict) -> None:
    print("═══════════════════════════════════════════════════════════════════")
    print("  DE10-Nano OTA Firmware Status")
    print("═══════════════════════════════════════════════════════════════════")
    print(f"  Active slot:     {payload.get('active_slot')} "
          f"(v{payload.get('active_version')})")
    if payload.get("active_description"):
        print(f"  Description:     {payload.get('active_description')}")
    print(f"  Standby slot:    {payload.get('standby_slot')} "
          f"(v{payload.get('standby_version') or 'empty'})")
    print(f"  Boot count:      {payload.get('boot_count')}")
    last_rb = payload.get("last_rollback")
    if last_rb:
        print(f"  Last rollback:   {last_rb.get('failed_slot')} -> "
              f"{last_rb.get('restored_slot')} "
              f"[{last_rb.get('stage')}: {last_rb.get('reason')}]")
    else:
        print("  Last rollback:   none")
    print("═══════════════════════════════════════════════════════════════════")


def print_update_result(code: int, payload: dict) -> None:
    print("───────────────────────────────────────────────────────────────────")
    for step in payload.get("steps", []):
        print(f"  • {step}")
    print("───────────────────────────────────────────────────────────────────")
    status = payload.get("status", "unknown")
    if status == "committed":
        print(f"  [SUCCESS] Update committed to {payload.get('active_slot')} "
              f"(v{payload.get('active_version')}, LED={payload.get('led_pattern')})")
    elif status == "rolled_back":
        print(f"  [ROLLED BACK] Failed at '{payload.get('failed_stage')}': "
              f"{payload.get('reason')}")
        print(f"  [RECOVERED]   Board automatically restored {payload.get('active_slot')} "
              f"(v{payload.get('active_version')}, LED={payload.get('led_pattern')})")
    elif status == "rejected":
        print(f"  [REJECTED]    Bundle rejected at '{payload.get('failed_stage')}': "
              f"{payload.get('reason')}")
        print(f"  [UNCHANGED]   Active slot remains {payload.get('active_slot')} "
              f"(v{payload.get('active_version')})")
    else:
        print(f"  [HTTP {code}] {payload}")


def main() -> int:
    parser = argparse.ArgumentParser(
        description="PC-side OTA field-update client for DE10-Nano."
    )
    parser.add_argument("--host", required=True,
                        help="Board IP or hostname (IPv4 or IPv6)")
    parser.add_argument("--port", type=int, default=DEFAULT_OTA_PORT,
                        help=f"OTA daemon HTTP port (default: {DEFAULT_OTA_PORT})")

    sub = parser.add_subparsers(dest="cmd", required=True)
    sub.add_parser("status", help="Query active/standby slots and rollback state")

    push_p = sub.add_parser("push", help="Upload and apply a .tar.gz firmware bundle")
    push_p.add_argument("bundle", help="Path to .tar.gz firmware bundle")
    push_p.add_argument("--expect-status", choices=["committed", "rolled_back", "rejected"],
                        default="committed",
                        help="Expected outcome status for automated testing (default: committed)")

    args = parser.parse_args()
    base_url = format_base_url(args.host, args.port)

    try:
        if args.cmd == "status":
            code, payload = http_json_request(f"{base_url}/status")
            if code != 200:
                print(f"Error querying status (HTTP {code}): {payload}", file=sys.stderr)
                return 1
            print_status(payload)
            return 0

        if args.cmd == "push":
            with open(args.bundle, "rb") as f:
                bundle_bytes = f.read()
            print(f"Uploading {args.bundle} ({len(bundle_bytes)} bytes) to {base_url}/update ...")
            code, payload = http_json_request(
                f"{base_url}/update", method="POST", data=bundle_bytes
            )
            print_update_result(code, payload)
            actual_status = payload.get("status")
            return 0 if actual_status == args.expect_status else 1

    except OSError as exc:
        print(f"Connection error to {base_url}: {exc}", file=sys.stderr)
        return 1

    return 0


if __name__ == "__main__":
    sys.exit(main())

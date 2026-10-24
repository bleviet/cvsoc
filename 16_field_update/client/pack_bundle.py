#!/usr/bin/env python3
"""
pack_bundle.py — Package an FPGA bitstream (.rbf) and HPS binary (led_server)
into an OTA field-update bundle (.tar.gz) with a SHA-256 manifest.json.

Supports fault-injection flags for Part 3 rollback testing:
  --corrupt-payload   Flip a byte in de10_nano.rbf AFTER computing manifest
                      checksums (simulates network/storage bit-rot).
  --bad-rbf           Replace de10_nano.rbf with an invalid/uncompressed RBF
                      payload while keeping its SHA-256 valid in manifest.json
                      (simulates a bad bitstream that fails FPGA CONF_DONE).
"""

import argparse
import hashlib
import io
import json
import os
import stat
import sys
import tarfile
import time


def sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def create_bundle(
    rbf_bytes: bytes,
    app_bytes: bytes,
    version: str,
    description: str,
    output_path: str,
    corrupt_payload: bool = False,
    bad_rbf: bool = False,
) -> dict:
    """Create a .tar.gz OTA bundle and return the manifest dictionary."""
    if bad_rbf:
        # Simulate a corrupted/uncompressed RBF bitstream that has a VALID
        # manifest.json SHA-256 (passes network checksum) but causes the
        # Cyclone V FPGA configuration controller to pull nSTATUS LOW / fail
        # CONF_DONE during hardware programming.
        bad_buf = bytearray(rbf_bytes if len(rbf_bytes) >= 1024 else b"\xFF" * 4096)
        bad_buf[0:32] = b"UNCOMPRESSED_INVALID_RBF_HEADER\x00"
        for off in range(32, min(len(bad_buf), 65536), 4):
            bad_buf[off:off + 4] = b"\xDE\xAD\xBE\xEF"
        rbf_bytes = bytes(bad_buf)

    manifest = {
        "schema_version": 1,
        "version": version,
        "description": description,
        "created_at": int(time.time()),
        "files": {
            "de10_nano.rbf": {
                "sha256": sha256_bytes(rbf_bytes),
                "size": len(rbf_bytes),
            },
            "led_server": {
                "sha256": sha256_bytes(app_bytes),
                "size": len(app_bytes),
            },
        },
    }

    if corrupt_payload:
        # Flip one byte in the payload AFTER recording the legitimate SHA-256
        # in manifest.json so the board's integrity check catches the corruption.
        mutable = bytearray(rbf_bytes)
        mutable[0] ^= 0xFF
        rbf_bytes = bytes(mutable)

    manifest_bytes = (json.dumps(manifest, indent=2) + "\n").encode("utf-8")

    os.makedirs(os.path.dirname(os.path.abspath(output_path)), exist_ok=True)
    with tarfile.open(output_path, "w") as tar:
        for name, content, mode in [
            ("manifest.json", manifest_bytes, 0o644),
            ("de10_nano.rbf", rbf_bytes, 0o644),
            ("led_server", app_bytes, 0o755),
        ]:
            info = tarfile.TarInfo(name=name)
            info.size = len(content)
            info.mode = mode
            info.mtime = manifest["created_at"]
            tar.addfile(info, io.BytesIO(content))

    return manifest


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Package FPGA .rbf + ARM application into an OTA update bundle."
    )
    parser.add_argument("--rbf", required=True, help="Path to de10_nano.rbf")
    parser.add_argument("--app", required=True, help="Path to led_server binary")
    parser.add_argument("--version", required=True, help="Firmware version (e.g. 2.0.0)")
    parser.add_argument(
        "--description",
        default="OTA firmware update bundle",
        help="Human-readable release description",
    )
    parser.add_argument("--output", required=True, help="Output .tar.gz bundle path")
    parser.add_argument(
        "--corrupt-payload",
        action="store_true",
        help="Fault injection: flip a byte after computing SHA-256 in manifest.json",
    )
    parser.add_argument(
        "--bad-rbf",
        action="store_true",
        help="Fault injection: package an invalid .rbf with a valid manifest SHA-256",
    )
    args = parser.parse_args()

    try:
        with open(args.rbf, "rb") as f:
            rbf_bytes = f.read()
        with open(args.app, "rb") as f:
            app_bytes = f.read()
    except OSError as exc:
        print(f"Error reading input files: {exc}", file=sys.stderr)
        return 1

    manifest = create_bundle(
        rbf_bytes=rbf_bytes,
        app_bytes=app_bytes,
        version=args.version,
        description=args.description,
        output_path=args.output,
        corrupt_payload=args.corrupt_payload,
        bad_rbf=args.bad_rbf,
    )

    print(f"Created OTA bundle: {args.output}")
    print(f"  Version:       {manifest['version']}")
    print(f"  de10_nano.rbf: {manifest['files']['de10_nano.rbf']['sha256'][:16]}... "
          f"({manifest['files']['de10_nano.rbf']['size']} bytes)")
    print(f"  led_server:    {manifest['files']['led_server']['sha256'][:16]}... "
          f"({manifest['files']['led_server']['size']} bytes)")
    if args.corrupt_payload:
        print("  [FAULT INJECTION] Payload corrupted post-manifest (SHA-256 mismatch)")
    if args.bad_rbf:
        print("  [FAULT INJECTION] Invalid/uncompressed RBF packaged (CONF_DONE fault)")
    return 0


if __name__ == "__main__":
    sys.exit(main())

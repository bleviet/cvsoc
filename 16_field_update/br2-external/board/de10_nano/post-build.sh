#!/bin/bash
# post-build.sh — Runs after Buildroot builds the root filesystem for Phase 10
# (16_field_update).
#
# Installs init scripts, ota_daemon.py, and populates /opt/firmware/slot_a with
# the Golden v1.0.0 FPGA bitstream and led_server binary so the board boots
# directly into slot_a on first power-up.

set -e

TARGET_DIR="$1"
BOARD_DIR="$(dirname "$0")"
EXTERNAL_DIR="$(dirname "$(dirname "$BOARD_DIR")")"
PROJECT_DIR="$(dirname "$EXTERNAL_DIR")"

# Install init scripts
install -D -m 0755 "${BOARD_DIR}/S30fpga_load" \
    "${TARGET_DIR}/etc/init.d/S30fpga_load"
install -D -m 0755 "${BOARD_DIR}/S40led_server" \
    "${TARGET_DIR}/etc/init.d/S40led_server"
install -D -m 0755 "${BOARD_DIR}/S50ota_daemon" \
    "${TARGET_DIR}/etc/init.d/S50ota_daemon"

# Install OTA update daemon
install -D -m 0755 "${PROJECT_DIR}/daemon/ota_daemon.py" \
    "${TARGET_DIR}/usr/bin/ota_daemon.py"

# Locate FPGA bitstream (reused from Phase 6 / 10_linux_led)
RBF_FILE="${PROJECT_DIR}/de10_nano.rbf"
if [ ! -f "$RBF_FILE" ]; then
    RBF_FILE="${PROJECT_DIR}/../10_linux_led/de10_nano.rbf"
fi

FW_ROOT="${TARGET_DIR}/opt/firmware"
mkdir -p "${FW_ROOT}/slot_a" "${FW_ROOT}/slot_b"

if [ -f "$RBF_FILE" ]; then
    install -D -m 0644 "$RBF_FILE" "${TARGET_DIR}/lib/firmware/de10_nano.rbf"
    install -D -m 0644 "$RBF_FILE" "${FW_ROOT}/slot_a/de10_nano.rbf"
fi

if [ -f "${TARGET_DIR}/usr/bin/led_server" ]; then
    install -D -m 0755 "${TARGET_DIR}/usr/bin/led_server" "${FW_ROOT}/slot_a/led_server"
fi

# Generate Golden v1.0.0 manifest.json and initial state.json
python3 - "${FW_ROOT}" <<'PYEOF'
import hashlib, json, os, sys, time

fw_root = sys.argv[1]
slot_a = os.path.join(fw_root, "slot_a")

files_meta = {}
for name in ("de10_nano.rbf", "led_server"):
    p = os.path.join(slot_a, name)
    if os.path.isfile(p):
        with open(p, "rb") as f:
            data = f.read()
        files_meta[name] = {
            "sha256": hashlib.sha256(data).hexdigest(),
            "size": len(data),
        }

manifest = {
    "schema_version": 1,
    "version": "1.0.0",
    "description": "Factory Golden v1.0.0 firmware (startup LED pattern 0x0F)",
    "created_at": int(time.time()),
    "files": files_meta,
}
with open(os.path.join(slot_a, "manifest.json"), "w") as f:
    json.dump(manifest, f, indent=2)
    f.write("\n")

state = {
    "active_slot": "slot_a",
    "pending_slot": None,
    "boot_count": 0,
    "last_rollback": None,
    "history": [],
}
with open(os.path.join(fw_root, "state.json"), "w") as f:
    json.dump(state, f, indent=2)
    f.write("\n")
PYEOF

ln -sfn "slot_a" "${FW_ROOT}/active"
echo "post-build.sh: /opt/firmware/slot_a (v1.0.0) and OTA daemon installed"

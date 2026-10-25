#!/bin/bash
# post-build.sh — Runs after Buildroot builds the root filesystem for Phase 11.

set -e

TARGET_DIR="$1"
BOARD_DIR="$(dirname "$0")"
EXTERNAL_DIR="$(dirname "$(dirname "$BOARD_DIR")")"
PROJECT_DIR="$(dirname "$EXTERNAL_DIR")"

install -D -m 0755 "${BOARD_DIR}/S40led_opcua_server" \
    "${TARGET_DIR}/etc/init.d/S40led_opcua_server"
echo "post-build.sh: S40led_opcua_server init script installed"

PHASE6_DTS="${PROJECT_DIR}/../10_linux_led/dts/fpga_led_overlay.dts"
if [ -f "$PHASE6_DTS" ]; then
    install -D -m 0644 "$PHASE6_DTS" \
        "${TARGET_DIR}/usr/share/fpga-led/fpga_led_overlay.dts"
fi

RBF_FILE="${PROJECT_DIR}/de10_nano.rbf"
if [ ! -f "$RBF_FILE" ]; then
    RBF_FILE="${PROJECT_DIR}/../10_linux_led/de10_nano.rbf"
fi
if [ -f "$RBF_FILE" ]; then
    install -D -m 0644 "$RBF_FILE" "${TARGET_DIR}/lib/firmware/de10_nano.rbf"
    echo "post-build.sh: de10_nano.rbf installed to /lib/firmware/"
else
    echo "post-build.sh: WARNING — de10_nano.rbf not found"
    echo "  Build Phase 6 first: cd ../10_linux_led && make rbf"
fi

install -D -m 0755 "${BOARD_DIR}/S30fpga_load" \
    "${TARGET_DIR}/etc/init.d/S30fpga_load"
echo "post-build.sh: S30fpga_load init script installed"

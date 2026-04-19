#!/bin/bash
# post-build.sh — Runs after Buildroot builds the root filesystem.
#
# Copies custom files into the target rootfs before it is packed into ext4.
# Called by Buildroot with TARGET_DIR as $1.

set -e

TARGET_DIR="$1"
BOARD_DIR="$(dirname "$0")"
EXTERNAL_DIR="$(dirname "$(dirname "$BOARD_DIR")")"
PROJECT_DIR="$(dirname "$EXTERNAL_DIR")"

# Install the Python mmap LED script
install -D -m 0755 "${PROJECT_DIR}/software/fpga_led.py" \
    "${TARGET_DIR}/usr/bin/fpga_led.py"

# Install the device tree overlay source for reference
install -D -m 0644 "${PROJECT_DIR}/dts/fpga_led_overlay.dts" \
    "${TARGET_DIR}/usr/share/fpga-led/fpga_led_overlay.dts"

echo "post-build.sh: custom files installed into rootfs"

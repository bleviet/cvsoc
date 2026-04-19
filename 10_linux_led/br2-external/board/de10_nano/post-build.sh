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

# Copy FPGA bitstream to /lib/firmware/ so the FPGA Manager can find it
RBF_FILE="${PROJECT_DIR}/de10_nano.rbf"
if [ -f "$RBF_FILE" ]; then
    install -D -m 0644 "$RBF_FILE" "${TARGET_DIR}/lib/firmware/de10_nano.rbf"
    echo "post-build.sh: de10_nano.rbf installed to /lib/firmware/"
else
    echo "post-build.sh: WARNING — de10_nano.rbf not found at ${RBF_FILE}"
    echo "  Run: quartus_cpf -c --option=bitstream_compression=on de10_nano.sof de10_nano.rbf"
fi

# Install S30fpga_load init script to program FPGA and enable bridge at boot
install -D -m 0755 "${BOARD_DIR}/S30fpga_load" \
    "${TARGET_DIR}/etc/init.d/S30fpga_load"
echo "post-build.sh: S30fpga_load init script installed"

echo "post-build.sh: custom files installed into rootfs"

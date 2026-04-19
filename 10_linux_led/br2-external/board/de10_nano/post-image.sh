#!/bin/bash
# post-image.sh — Runs after Buildroot generates filesystem images.
#
# Compiles the device tree overlay and copies all boot artifacts to the
# images directory before genimage assembles the final SD card image.
#
# Buildroot sets these environment variables before calling this script:
#   BINARIES_DIR — path to output/images/
#   HOST_DIR     — path to output/host/ (contains host tools like dtc)
#   BUILD_DIR    — path to output/build/
#   BASE_DIR     — path to output/

set -e

IMAGES_DIR="${BINARIES_DIR}"
BOARD_DIR="$(dirname "$0")"
EXTERNAL_DIR="$(dirname "$(dirname "$BOARD_DIR")")"
PROJECT_DIR="$(dirname "$EXTERNAL_DIR")"

# Locate the host dtc built by Buildroot
DTC="${HOST_DIR}/bin/dtc"
if [ ! -x "$DTC" ]; then
    DTC="$(which dtc 2>/dev/null || true)"
fi

# Compile the device tree overlay (.dts → .dtbo)
DTS_FILE="${PROJECT_DIR}/dts/fpga_led_overlay.dts"
DTBO_FILE="${IMAGES_DIR}/fpga_led_overlay.dtbo"

if [ -f "$DTS_FILE" ] && [ -x "$DTC" ]; then
    echo "post-image.sh: compiling device tree overlay..."
    "$DTC" -I dts -O dtb \
        -o "$DTBO_FILE" \
        -@ "$DTS_FILE" 2>/dev/null || true
    echo "post-image.sh: ${DTBO_FILE} created"

    # Merge UIO node directly into base DTB (kernel DTB lacks __symbols__)
    BASE_DTB="${IMAGES_DIR}/socfpga_cyclone5_de0_nano_soc.dtb"
    if [ -f "$BASE_DTB" ]; then
        echo "post-image.sh: merging UIO node into base DTB..."
        MERGED_DTS="${IMAGES_DIR}/merged.dts"
        # Decompile base DTB
        "$DTC" -I dtb -O dts -o "$MERGED_DTS" "$BASE_DTB" 2>/dev/null
        # Inject led-controller node inside base_fpga_region (before its closing brace)
        sed -i '/base_fpga_region {/,/^[[:space:]]*};/{
            /^[[:space:]]*};/{
                i\
\t\t\tled-controller@ff200000 {\
\t\t\t\tcompatible = "generic-uio";\
\t\t\t\treg = <0xff200000 0x10>;\
\t\t\t\tstatus = "okay";\
\t\t\t};
            }
        }' "$MERGED_DTS"
        # Recompile merged DTS to DTB
        "$DTC" -I dts -O dtb -o "$BASE_DTB" "$MERGED_DTS" 2>/dev/null || true
        rm -f "$MERGED_DTS"
        echo "post-image.sh: UIO node merged into ${BASE_DTB}"
    fi
elif [ -f "$DTS_FILE" ]; then
    echo "post-image.sh: WARNING — dtc not found, skipping overlay compilation"
fi

# Copy the FPGA bitstream (.rbf) to images if available
RBF_FILE="${PROJECT_DIR}/de10_nano.rbf"
if [ -f "$RBF_FILE" ]; then
    cp "$RBF_FILE" "${IMAGES_DIR}/de10_nano.rbf"
    echo "post-image.sh: copied FPGA bitstream to ${IMAGES_DIR}"
else
    echo "post-image.sh: WARNING — de10_nano.rbf not found, FPGA must be loaded manually"
fi

# Create extlinux directory with boot config for U-Boot distro boot
mkdir -p "${IMAGES_DIR}/extlinux"
cp "${BOARD_DIR}/extlinux.conf" "${IMAGES_DIR}/extlinux/extlinux.conf"
echo "post-image.sh: extlinux.conf installed"

echo "post-image.sh: boot artifacts prepared in ${IMAGES_DIR}"

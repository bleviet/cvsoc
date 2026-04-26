#!/bin/bash
# sign_firmware.sh — Builds and signs a FIT (Flattened Image Tree) image
#
# This script uses an Ubuntu Docker container to install U-Boot tools
# and the Device Tree Compiler, then builds a .itb firmware image
# containing the Linux kernel, device tree, and FPGA bitstream.
# It signs the image using the provided RSA private key.
#
# Usage:
#   ./sign_firmware.sh <its_file> <keys_dir> [output.itb]

set -euo pipefail

ITS_FILE="${1:-fit_image.its}"
KEYS_DIR="${2:-keys}"
OUTPUT_ITB="${3:-signed_firmware.itb}"

if [ ! -f "$ITS_FILE" ]; then
    echo "Error: ITS file not found: $ITS_FILE"
    exit 1
fi

if [ ! -d "$KEYS_DIR" ]; then
    echo "Error: Keys directory not found: $KEYS_DIR"
    echo "Run 'make rsa_keys' first."
    exit 1
fi

echo "═══════════════════════════════════════════════════════════════"
echo "  Building and Signing Firmware (U-Boot FIT Image)"
echo "═══════════════════════════════════════════════════════════════"
echo "ITS File: ${ITS_FILE}"
echo "Keys Dir: ${KEYS_DIR}"
echo "Output:   ${OUTPUT_ITB}"
echo ""
echo "Note: Downloading required tools (u-boot-tools, device-tree-compiler) inside Docker..."

# Run mkimage in an Ubuntu container so we have the required tools
docker run --rm --user root \
    -v "$(pwd)/..:/work" \
    -w /work/13_secure_boot \
    ubuntu:22.04 \
    bash -c "
        apt-get update -qq && \
        DEBIAN_FRONTEND=noninteractive apt-get install -y -qq u-boot-tools device-tree-compiler openssl >/dev/null && \
        mkimage -f \"${ITS_FILE}\" -k \"${KEYS_DIR}\" -r \"${OUTPUT_ITB}\" && \
        chown \$(stat -c %u \"${ITS_FILE}\"):\$(stat -c %g \"${ITS_FILE}\") \"${OUTPUT_ITB}\"
    "

echo ""
echo "Done! Generated signed firmware:"
echo "  ${OUTPUT_ITB}"
echo ""
echo "This .itb file contains the kernel, DTB, and FPGA bitstream,"
echo "and is digitally signed with your RSA key."

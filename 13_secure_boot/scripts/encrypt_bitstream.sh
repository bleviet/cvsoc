#!/bin/bash
# encrypt_bitstream.sh — Encrypt a Quartus .sof into a secured .rbf format
#
# Uses quartus_cpf to encrypt an unencrypted SRAM Object File (.sof)
# using AES-256 CBC. It outputs an encrypted Raw Binary File (.rbf)
# for the FPGA Manager and an Encryption Key Programming File (.ekp)
# which is used by quartus_pgm to program the BBRAM or eFuse.
#
# Usage:
#   ./encrypt_bitstream.sh <key_file> <key_id> <input.sof> [output.rbf]
#
# This script is designed to run inside the cvsoc/quartus:23.1 Docker
# container where quartus_cpf is available.

set -euo pipefail

if [ $# -lt 3 ]; then
    echo "Usage: $0 <key_file.key> <key_id> <input.sof> [output.rbf]"
    exit 1
fi

KEY_FILE="$1"
KEY_ID="$2"
SOF_FILE="$3"
RBF_FILE="${4:-${SOF_FILE%.sof}_encrypted.rbf}"

if [ ! -f "$KEY_FILE" ]; then
    echo "Error: Key file not found: $KEY_FILE"
    exit 1
fi

if [ ! -f "$SOF_FILE" ]; then
    echo "Error: Input bitstream not found: $SOF_FILE"
    exit 1
fi

echo "═══════════════════════════════════════════════════════════════"
echo "  Encrypting FPGA Bitstream (AES-256)"
echo "═══════════════════════════════════════════════════════════════"
echo "Key:    ${KEY_FILE} (ID: ${KEY_ID})"
echo "Input:  ${SOF_FILE}"
echo "Output: ${RBF_FILE}"
echo ""

# The conversion command
set +e
quartus_cpf -c \
    --key "${KEY_FILE}:${KEY_ID}" \
    --option=bitstream_compression=on \
    --option=create_ekp_file=on \
    "$SOF_FILE" "$RBF_FILE"
CPF_EXIT_CODE=$?
set -e

if [ $CPF_EXIT_CODE -ne 0 ]; then
    echo "═══════════════════════════════════════════════════════════════"
    echo "  NOTE ON QUARTUS LITE EDITION"
    echo "═══════════════════════════════════════════════════════════════"
    echo "If you saw 'Error (213052): Design security feature is not enabled',"
    echo "this is expected! Bitstream encryption (AES-256) requires"
    echo "Quartus Prime Standard or Pro Edition."
    echo ""
    echo "Quartus Prime Lite (used in this tutorial) does not support"
    echo "generating encrypted .rbf or .ekp files."
    echo ""
    echo "This script demonstrates the exact workflow used in industry."
    echo "In a production environment with Quartus Standard/Pro, this"
    echo "command would generate your encrypted .rbf and .ekp files."
    exit 0
fi

# quartus_cpf auto-generates the .ekp using the input file's basename
# Let's rename it to match our intended output naming
EXPECTED_EKP="${SOF_FILE%.sof}.ekp"
FINAL_EKP="${RBF_FILE%.rbf}.ekp"

if [ -f "$EXPECTED_EKP" ] && [ "$EXPECTED_EKP" != "$FINAL_EKP" ]; then
    mv "$EXPECTED_EKP" "$FINAL_EKP"
fi

echo ""
echo "Done! Generated:"
echo "  1. Encrypted bitstream: $RBF_FILE"
echo "  2. Key programming file: $FINAL_EKP"
echo ""
echo "Next step: Program the .ekp to volatile BBRAM using quartus_pgm,"
echo "           then load the encrypted .rbf from Linux or U-Boot."

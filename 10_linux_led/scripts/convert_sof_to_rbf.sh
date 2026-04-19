#!/bin/bash
# convert_sof_to_rbf.sh — Convert a Quartus .sof bitstream to .rbf format.
#
# The FPGA Manager in Linux expects a Raw Binary File (.rbf) to load the
# FPGA fabric.  This script uses quartus_cpf to perform the conversion.
#
# IMPORTANT: bitstream_compression=on is REQUIRED for the DE10-Nano because
# the MSEL switches are set to 0x0A (PP32_FAST_AESOPT_DC) which means the
# FPGA's decompression engine is active and expects compressed data.
#
# Usage:
#   ./convert_sof_to_rbf.sh <input.sof> [output.rbf]
#
# If output.rbf is not specified, the output file name is derived from the
# input by replacing the .sof extension with .rbf.
#
# This script is designed to run inside the raetro/quartus:23.1 Docker
# container where quartus_cpf is available.

set -euo pipefail

if [ $# -lt 1 ]; then
    echo "Usage: $0 <input.sof> [output.rbf]"
    exit 1
fi

SOF_FILE="$1"
RBF_FILE="${2:-${SOF_FILE%.sof}.rbf}"

if [ ! -f "$SOF_FILE" ]; then
    echo "Error: input file not found: $SOF_FILE"
    exit 1
fi

echo "Converting: $SOF_FILE → $RBF_FILE"

quartus_cpf -c \
    --option=bitstream_compression=on \
    "$SOF_FILE" "$RBF_FILE"

echo "Done: $RBF_FILE ($(stat -c %s "$RBF_FILE") bytes)"

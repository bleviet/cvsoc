#!/bin/bash
# generate_keys.sh — Generates a 256-bit AES key for Quartus bitstream encryption
#
# This script generates a random 256-bit (64 hex characters) key
# and writes it to a .key file in the format expected by quartus_cpf:
#   KEY <key_id> = <64_hex_digits>;
#
# Usage:
#   ./generate_keys.sh [output.key] [key_id]

set -euo pipefail

OUTPUT_FILE="${1:-aes_key.key}"
KEY_ID="${2:-key1}"

# Generate 32 random bytes (256 bits) and format as an uppercase hex string
if command -v openssl >/dev/null 2>&1; then
    HEX_KEY=$(openssl rand -hex 32 | tr '[:lower:]' '[:upper:]')
else
    # Fallback if openssl is not available
    HEX_KEY=$(head -c 32 /dev/urandom | xxd -p | tr '[:lower:]' '[:upper:]')
fi

# Write the key file in the exact format Quartus requires
echo "KEY ${KEY_ID} = ${HEX_KEY};" > "${OUTPUT_FILE}"

echo "═══════════════════════════════════════════════════════════════"
echo "  Success: Generated AES-256 encryption key"
echo "═══════════════════════════════════════════════════════════════"
echo "File:   ${OUTPUT_FILE}"
echo "Key ID: ${KEY_ID}"
echo "Format: KEY ${KEY_ID} = ${HEX_KEY};"
echo ""
echo "WARNING: Keep this file secure. For this educational tutorial,"
echo "we generate a new random key each time. In a real product,"
echo "this key is the root of your bitstream security."

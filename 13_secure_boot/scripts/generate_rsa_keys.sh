#!/bin/bash
# generate_rsa_keys.sh — Generates an RSA key pair for FIT image signing
#
# This script generates a 2048-bit RSA private key and extracts
# the corresponding public key. These keys are used by mkimage
# to sign the Flattened Image Tree (FIT) image.
#
# Usage:
#   ./generate_rsa_keys.sh [keys_dir] [key_name]

set -euo pipefail

KEYS_DIR="${1:-keys}"
KEY_NAME="${2:-dev_key}"

mkdir -p "$KEYS_DIR"

PRIVATE_KEY="${KEYS_DIR}/${KEY_NAME}.key"
PUBLIC_KEY="${KEYS_DIR}/${KEY_NAME}.pub"

# Generate 2048-bit RSA private key
openssl genrsa -out "$PRIVATE_KEY" 2048 2>/dev/null

# Extract the public key
openssl rsa -in "$PRIVATE_KEY" -pubout -out "$PUBLIC_KEY" 2>/dev/null

echo "═══════════════════════════════════════════════════════════════"
echo "  Success: Generated RSA Key Pair for Firmware Signing"
echo "═══════════════════════════════════════════════════════════════"
echo "Private Key: ${PRIVATE_KEY}"
echo "Public Key:  ${PUBLIC_KEY}"
echo ""
echo "Note: The private key is used by mkimage to sign the FIT image."
echo "      The public key is typically compiled into U-Boot's Device"
echo "      Tree so the bootloader can verify the signature."

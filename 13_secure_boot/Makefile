# Makefile — Phase 9 Cybersecurity (Educational Secure Boot)
#
# Generates development encryption keys and encrypts a bitstream from
# a previous phase using AES-256 for the Cyclone V SoC.

PROJECT_NAME := 13_secure_boot
REPO_ROOT    := $(realpath ..)
DOCKER_IMAGE := cvsoc/quartus:23.1

# Use the Phase 7/6 bitstream as our input
INPUT_SOF    := $(REPO_ROOT)/05_hps_led/quartus/de10_nano.sof
KEY_FILE     := aes_key.key
KEY_ID       := dev_key_1
OUTPUT_RBF   := de10_nano_encrypted.rbf
OUTPUT_EKP   := de10_nano_encrypted.ekp
RSA_KEYS_DIR := keys
OUTPUT_ITB   := signed_firmware.itb

.PHONY: all keys encrypt rsa_keys sign_fit clean help

help:
	@echo "Phase 9 — Cybersecurity (Secure Boot) Targets:"
	@echo ""
	@echo "  make keys      — Generate a random AES-256 development key for FPGA"
	@echo "  make encrypt   — Encrypt the Phase 3 (.sof) bitstream using the AES key"
	@echo "  make rsa_keys  — Generate RSA-2048 key pair for FIT image signing"
	@echo "  make sign_fit  — Build and sign a Flattened Image Tree (FIT) firmware"
	@echo "  make all       — Run all key generation, encryption, and signing"
	@echo "  make clean     — Delete generated keys, encrypted bitstreams, and .itb"
	@echo ""
	@echo "Note: The encryption step runs inside the cvsoc/quartus:23.1 Docker container."
	@echo "      The FIT signing step runs inside a generic ubuntu:22.04 container."

all: keys encrypt rsa_keys sign_fit

# ── 1. AES Key Generation (FPGA Encryption) ───────────────────────────────────
# We run this locally since it just requires OpenSSL or /dev/urandom
keys: $(KEY_FILE)

$(KEY_FILE):
	@bash scripts/generate_keys.sh $(KEY_FILE) $(KEY_ID)

# ── 2. Bitstream Encryption (FPGA) ────────────────────────────────────────────
# Depends on the .sof from Phase 3 being compiled
$(INPUT_SOF):
	@echo "═══════════════════════════════════════════════════════════════"
	@echo "  Building missing dependency: 05_hps_led FPGA bitstream (.sof)"
	@echo "═══════════════════════════════════════════════════════════════"
	$(MAKE) -C $(REPO_ROOT)/05_hps_led/quartus all

encrypt: $(OUTPUT_RBF)

$(OUTPUT_RBF): $(KEY_FILE) $(INPUT_SOF) scripts/encrypt_bitstream.sh
	@docker run --rm --user $$(id -u):$$(id -g) \
	  -v $(REPO_ROOT):/work \
	  $(DOCKER_IMAGE) \
	  bash -c "cd /work/13_secure_boot && bash scripts/encrypt_bitstream.sh \
	           $(KEY_FILE) $(KEY_ID) /work/05_hps_led/quartus/de10_nano.sof $(OUTPUT_RBF)"

# ── 3. RSA Key Generation (Firmware Signing) ──────────────────────────────────
rsa_keys: $(RSA_KEYS_DIR)/dev_key.pub

$(RSA_KEYS_DIR)/dev_key.pub: scripts/generate_rsa_keys.sh
	@bash scripts/generate_rsa_keys.sh $(RSA_KEYS_DIR) dev_key

# ── 4. Firmware FIT Image Signing (Software) ──────────────────────────────────
sign_fit: $(OUTPUT_ITB)

$(OUTPUT_ITB): rsa_keys fit_image.its scripts/sign_firmware.sh
	@bash scripts/sign_firmware.sh fit_image.its $(RSA_KEYS_DIR) $(OUTPUT_ITB)

# ── Cleanup ───────────────────────────────────────────────────────────────────
clean:
	@echo "Cleaning up generated keys and encrypted bitstreams..."
	rm -f $(KEY_FILE) $(OUTPUT_RBF) $(OUTPUT_EKP) $(OUTPUT_ITB)
	rm -rf $(RSA_KEYS_DIR)
	@echo "Clean complete."

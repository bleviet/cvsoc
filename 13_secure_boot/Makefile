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

.PHONY: all keys encrypt clean help

help:
	@echo "Phase 9 — Cybersecurity (Secure Boot) Targets:"
	@echo ""
	@echo "  make keys     — Generate a random AES-256 development key"
	@echo "  make encrypt  — Encrypt the Phase 3 (.sof) bitstream using the key"
	@echo "  make all      — Run both 'keys' and 'encrypt'"
	@echo "  make clean    — Delete generated keys and encrypted bitstreams"
	@echo ""
	@echo "Note: The encryption step runs inside the cvsoc/quartus:23.1 Docker container."

all: keys encrypt

# ── 1. Key Generation ──────────────────────────────────────────────────────────
# We run this locally since it just requires OpenSSL or /dev/urandom
keys: $(KEY_FILE)

$(KEY_FILE):
	@bash scripts/generate_keys.sh $(KEY_FILE) $(KEY_ID)

# ── 2. Bitstream Encryption ───────────────────────────────────────────────────
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

# ── Cleanup ───────────────────────────────────────────────────────────────────
clean:
	@echo "Cleaning up generated keys and encrypted bitstreams..."
	rm -f $(KEY_FILE) $(OUTPUT_RBF) $(OUTPUT_EKP)
	@echo "Clean complete."

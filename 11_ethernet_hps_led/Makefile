# Makefile for 11_ethernet_hps_led — Phase 7 Track A: HPS Ethernet LED control.
#
# Orchestrates:
#   1. FPGA bitstream copy/verification (.rbf from Phase 6)
#   2. Buildroot Linux build (kernel, U-Boot, rootfs, SD card image)
#   3. Standalone cross-compile of led_server for quick iterations
#
# Usage (from this directory):
#   make all            — Build everything
#   make rbf            — Verify / copy FPGA bitstream from Phase 6
#   make buildroot      — Full Buildroot build
#   make server-cross   — Standalone ARM cross-compile of led_server
#   make test           — Run Python unit tests (no board required)
#   make clean          — Remove generated files
#   make help           — Show all targets
#
# The Buildroot build runs on the host.  The FPGA bitstream is taken from
# Phase 6 (05_hps_led) which must have been built before running this Makefile.

PROJECT_DIR   := $(CURDIR)
REPO_ROOT     := $(realpath ..)

# ── FPGA bitstream (reused from Phase 6 / 05_hps_led) ───────────────────────
# Phase 6 converts the .sof; Phase 7 reuses it unchanged.
PHASE6_RBF    := $(REPO_ROOT)/10_linux_led/de10_nano.rbf
RBF_FILE      := $(PROJECT_DIR)/de10_nano.rbf
DOCKER_IMAGE  ?= cvsoc/quartus:23.1

# ── Buildroot ────────────────────────────────────────────────────────────────
BUILDROOT_VERSION := 2024.11.1
BUILDROOT_TARBALL := buildroot-$(BUILDROOT_VERSION).tar.xz
BUILDROOT_URL     := https://buildroot.org/downloads/$(BUILDROOT_TARBALL)
BUILDROOT_DIR     := $(PROJECT_DIR)/buildroot-$(BUILDROOT_VERSION)
BR2_EXTERNAL_DIR  := $(PROJECT_DIR)/br2-external
BR2_OUTPUT_DIR    := $(BUILDROOT_DIR)/output
SDCARD_IMG        := $(BR2_OUTPUT_DIR)/images/sdcard.img

# ── Cross-compiler ───────────────────────────────────────────────────────────
ARM_CC ?= arm-linux-gnueabihf-gcc

.PHONY: all rbf buildroot-download buildroot-config buildroot sdcard \
        server-cross test flash clean help

all: rbf buildroot

help:
	@echo "Targets:"
	@echo "  all             — Build everything (rbf + Linux SD card image)"
	@echo "  rbf             — Copy/verify FPGA bitstream from Phase 6"
	@echo "  buildroot       — Full Buildroot build (kernel, U-Boot, rootfs, SD card)"
	@echo "  server-cross    — Standalone ARM cross-compile of led_server"
	@echo "  test            — Run Python unit tests (no board required)"
	@echo "  flash           — Write sdcard.img to SD card (set SDCARD=/dev/sdX)"
	@echo "  clean           — Remove generated files"
	@echo ""
	@echo "Output: $(SDCARD_IMG)"
	@echo ""
	@echo "PC client (after board boots):"
	@echo "  python3 client/send_led_pattern.py --host <board-ip> --pattern 0xA5"
	@echo "  python3 client/send_led_pattern.py --host <board-ip> --animate chase"

# ── FPGA bitstream ───────────────────────────────────────────────────────────
# Phase 7 reuses the bitstream built by Phase 6. If it is already present
# locally (e.g. from a previous build), use it directly; otherwise copy from
# the Phase 6 output.
rbf: $(RBF_FILE)

$(RBF_FILE):
	@if [ -f "$(PHASE6_RBF)" ]; then \
	    echo "Copying FPGA bitstream from Phase 6..."; \
	    cp "$(PHASE6_RBF)" "$@"; \
	    echo "FPGA bitstream: $@"; \
	else \
	    echo "ERROR: $(PHASE6_RBF) not found."; \
	    echo "Build Phase 6 first: cd ../10_linux_led && make rbf"; \
	    exit 1; \
	fi

# ── Buildroot download ────────────────────────────────────────────────────────
buildroot-download: $(BUILDROOT_DIR)/Makefile

$(BUILDROOT_DIR)/Makefile:
	@echo "Downloading Buildroot $(BUILDROOT_VERSION)..."
	wget -q --show-progress -O $(BUILDROOT_TARBALL) $(BUILDROOT_URL)
	tar xf $(BUILDROOT_TARBALL)
	rm -f $(BUILDROOT_TARBALL)
	@echo "Buildroot extracted to $(BUILDROOT_DIR)"

# ── PATH sanitization (WSL2 inherits Windows paths with spaces) ───────────────
CLEAN_PATH := $(shell echo "$$PATH" | tr ':' '\n' | grep -v ' ' | paste -sd:)

# ── Buildroot configuration ──────────────────────────────────────────────────
buildroot-config: $(BUILDROOT_DIR)/Makefile
	PATH="$(CLEAN_PATH)" $(MAKE) -C $(BUILDROOT_DIR) \
	    BR2_EXTERNAL=$(BR2_EXTERNAL_DIR) de10_nano_defconfig

# ── Buildroot full build ──────────────────────────────────────────────────────
buildroot: rbf buildroot-config
	PATH="$(CLEAN_PATH)" $(MAKE) -C $(BUILDROOT_DIR) \
	    fpga-led-dirclean fpga-mgr-load-dirclean led-server-dirclean
	PATH="$(CLEAN_PATH)" $(MAKE) -C $(BUILDROOT_DIR)
	@echo ""
	@echo "═══════════════════════════════════════════════════════════════════"
	@echo "  Build complete!"
	@echo "  SD card image: $(SDCARD_IMG)"
	@echo ""
	@echo "  Write to SD card:"
	@echo "    sudo dd if=$(SDCARD_IMG) of=/dev/sdX bs=4M status=progress"
	@echo ""
	@echo "  Then from PC:"
	@echo "    python3 client/send_led_pattern.py --host <board-ip> --pattern 0xA5"
	@echo "═══════════════════════════════════════════════════════════════════"

sdcard: buildroot

# ── Standalone cross-compile ──────────────────────────────────────────────────
# Useful for quick iterations: recompile and scp to the running board.
server-cross:
	docker run --rm \
	    -v $(REPO_ROOT):/work \
	    $(DOCKER_IMAGE) \
	    arm-linux-gnueabihf-gcc -O2 -Wall -Wextra \
	        -o /work/11_ethernet_hps_led/software/led_server/led_server \
	        /work/11_ethernet_hps_led/software/led_server/led_server.c
	@echo "Binary: software/led_server/led_server"
	@echo "  scp software/led_server/led_server root@<board-ip>:/usr/bin/"

# ── Python unit tests ─────────────────────────────────────────────────────────
test:
	cd client && python3 test_protocol.py

# ── Flash SD card ─────────────────────────────────────────────────────────────
SDCARD ?=

flash: $(SDCARD_IMG)
ifndef SDCARD
	$(error Set SDCARD=/dev/sdX, e.g.: make flash SDCARD=/dev/sdb)
endif
	@echo "Writing $(SDCARD_IMG) to $(SDCARD)..."
	@echo "Press Ctrl-C within 5 seconds to abort."
	@sleep 5
	sudo dd if=$(SDCARD_IMG) of=$(SDCARD) bs=4M status=progress conv=fsync
	@echo "Done. Insert SD card into DE10-Nano and power on."

# ── Clean ─────────────────────────────────────────────────────────────────────
clean:
	rm -f $(RBF_FILE)
	$(MAKE) -C software/led_server clean 2>/dev/null || true
	rm -rf buildroot-*
	@echo "Cleaned."

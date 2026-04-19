# Makefile for the 10_linux_led project (Phase 6 — Embedded Linux on HPS).
#
# Orchestrates:
#   1. FPGA bitstream conversion (.sof → .rbf)
#   2. Buildroot Linux build (kernel, U-Boot, rootfs, SD card image)
#
# Usage (from repo root):
#   cd 10_linux_led && make all
#
# The FPGA bitstream conversion runs inside the raetro/quartus:23.1 Docker
# container.  The Buildroot build runs on the host (requires ~4 GB disk,
# ~30 min on first build).
#
# Prerequisites:
#   - Docker (for quartus_cpf)
#   - wget, tar, make, gcc (for Buildroot host tools)
#   - Internet access (Buildroot downloads toolchain, kernel, U-Boot sources)

PROJECT_DIR   := $(CURDIR)
REPO_ROOT     := $(realpath ..)

# ── FPGA bitstream (reused from 05_hps_led) ──────────────────────────────────
SOF_FILE      := $(REPO_ROOT)/05_hps_led/quartus/de10_nano.sof
RBF_FILE      := $(PROJECT_DIR)/de10_nano.rbf
DOCKER_IMAGE  ?= raetro/quartus:23.1

# ── Buildroot ─────────────────────────────────────────────────────────────────
BUILDROOT_VERSION := 2024.11.1
BUILDROOT_TARBALL := buildroot-$(BUILDROOT_VERSION).tar.xz
BUILDROOT_URL     := https://buildroot.org/downloads/$(BUILDROOT_TARBALL)
BUILDROOT_DIR     := $(PROJECT_DIR)/buildroot-$(BUILDROOT_VERSION)
BR2_EXTERNAL_DIR  := $(PROJECT_DIR)/br2-external
BR2_OUTPUT_DIR    := $(BUILDROOT_DIR)/output

SDCARD_IMG        := $(BR2_OUTPUT_DIR)/images/sdcard.img

.PHONY: all rbf buildroot-download buildroot-config buildroot sdcard clean \
        help app-cross flash

all: rbf buildroot

help:
	@echo "Targets:"
	@echo "  all               — Build everything (FPGA .rbf + Linux SD card image)"
	@echo "  rbf               — Convert FPGA .sof to .rbf (requires Docker + Quartus)"
	@echo "  buildroot-download — Download Buildroot source tarball"
	@echo "  buildroot-config  — Apply DE10-Nano defconfig to Buildroot"
	@echo "  buildroot         — Full Buildroot build (kernel, U-Boot, rootfs, SD card)"
	@echo "  sdcard            — Alias for buildroot (produces sdcard.img)"
	@echo "  app-cross         — Cross-compile fpga_led.c standalone (outside Buildroot)"
	@echo "  clean             — Remove all generated files"
	@echo ""
	@echo "Output: $(SDCARD_IMG)"
	@echo ""
	@echo "Write to SD card:"
	@echo "  sudo dd if=$(SDCARD_IMG) of=/dev/sdX bs=4M status=progress"

# ── FPGA bitstream conversion (.sof → .rbf) ──────────────────────────────────
# Runs quartus_cpf inside Docker to convert the .sof bitstream from 05_hps_led
# into a Raw Binary File suitable for Linux FPGA Manager.
rbf: $(RBF_FILE)

$(RBF_FILE): $(SOF_FILE) scripts/convert_sof_to_rbf.sh
	docker run --rm \
	  -v $(REPO_ROOT):/work \
	  $(DOCKER_IMAGE) \
	  bash -c "cd /work/10_linux_led && bash scripts/convert_sof_to_rbf.sh \
	           /work/05_hps_led/quartus/de10_nano.sof /work/10_linux_led/de10_nano.rbf"
	@echo "FPGA bitstream: $(RBF_FILE)"

# ── Buildroot download ────────────────────────────────────────────────────────
buildroot-download: $(BUILDROOT_DIR)/Makefile

$(BUILDROOT_DIR)/Makefile:
	@echo "Downloading Buildroot $(BUILDROOT_VERSION)..."
	wget -q --show-progress -O $(BUILDROOT_TARBALL) $(BUILDROOT_URL)
	tar xf $(BUILDROOT_TARBALL)
	rm -f $(BUILDROOT_TARBALL)
	@echo "Buildroot extracted to $(BUILDROOT_DIR)"

# ── PATH sanitization (WSL2 inherits Windows paths with spaces) ───────────────
# Buildroot rejects PATH entries containing spaces/tabs/newlines.
CLEAN_PATH := $(shell echo "$$PATH" | tr ':' '\n' | grep -v ' ' | paste -sd:)

# ── Buildroot configuration ──────────────────────────────────────────────────
buildroot-config: $(BUILDROOT_DIR)/Makefile
	PATH="$(CLEAN_PATH)" $(MAKE) -C $(BUILDROOT_DIR) BR2_EXTERNAL=$(BR2_EXTERNAL_DIR) de10_nano_defconfig

# ── Buildroot full build ──────────────────────────────────────────────────────
buildroot: buildroot-config
	PATH="$(CLEAN_PATH)" $(MAKE) -C $(BUILDROOT_DIR)
	@echo ""
	@echo "═══════════════════════════════════════════════════════════════════"
	@echo "  Build complete!"
	@echo "  SD card image: $(SDCARD_IMG)"
	@echo ""
	@echo "  Write to SD card:"
	@echo "    sudo dd if=$(SDCARD_IMG) of=/dev/sdX bs=4M status=progress"
	@echo "═══════════════════════════════════════════════════════════════════"

sdcard: buildroot

# ── Flash SD card ─────────────────────────────────────────────────────────────
# Write sdcard.img to a real SD card.  Set SDCARD=/dev/sdX before running.
#   make flash SDCARD=/dev/sdb
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

# ── Cross-compile app standalone (without Buildroot) ──────────────────────────
# Useful for quick iterations: recompile the app, scp to the running board.
ARM_CC ?= arm-linux-gnueabihf-gcc

app-cross:
	$(MAKE) -C software/fpga_led CC=$(ARM_CC) CFLAGS="-O2 -Wall -Wextra"
	@echo "Binary: software/fpga_led/fpga_led"
	@echo "  scp software/fpga_led/fpga_led root@<board-ip>:/usr/bin/"

# ── Clean ─────────────────────────────────────────────────────────────────────
clean:
	rm -f $(RBF_FILE)
	rm -f dts/*.dtbo
	$(MAKE) -C software/fpga_led clean 2>/dev/null || true
	rm -rf buildroot-*
	@echo "Cleaned."

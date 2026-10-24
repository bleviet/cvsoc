# Makefile for 16_field_update — Phase 10: Network Field Update & Automatic Rollback
#
# Usage (from this directory):
#   make test                   — Run local end-to-end & fault-injection unit tests
#   make bundles                — Cross-compile v1/v2/crash apps and pack OTA bundles
#   make status HOST=<ip>       — Query active/standby slots on the DE10-Nano
#   make push-v2 HOST=<ip>      — Push v2.0.0 update over Ethernet (Part 1)
#   make push-v1 HOST=<ip>      — Push v1.0.0 update over Ethernet
#   make test-corrupt-transfer HOST=<ip> — Fault A: SHA-256 mismatch rejection (Part 3)
#   make test-bad-bitstream HOST=<ip>    — Fault B: Bad .rbf rollback (Part 3)
#   make test-bad-app HOST=<ip>          — Fault C: Crashing app rollback (Part 3)
#   make buildroot              — Build complete bootable SD card image (sdcard.img)

PROJECT_DIR   := $(CURDIR)
REPO_ROOT     := $(realpath ..)
PROJECT_NAME  = 16_field_update

QUARTUS_SUPPORTED ?= 23.1 25.1
NEEDS_SOFTWARE_TOOLS ?= 1

include ../common/make/quartus-version.mk

PHASE6_RBF    := $(REPO_ROOT)/10_linux_led/de10_nano.rbf
RBF_FILE      := $(PROJECT_DIR)/de10_nano.rbf
BUILD_DIR     := $(PROJECT_DIR)/build

BUILDROOT_VERSION := 2024.11.1
BUILDROOT_TARBALL := buildroot-$(BUILDROOT_VERSION).tar.xz
BUILDROOT_URL     := https://buildroot.org/downloads/$(BUILDROOT_TARBALL)
BUILDROOT_DIR     := $(PROJECT_DIR)/buildroot-$(BUILDROOT_VERSION)
BR2_EXTERNAL_DIR  := $(PROJECT_DIR)/br2-external
BR2_OUTPUT_DIR    := $(BUILDROOT_DIR)/output
SDCARD_IMG        := $(BR2_OUTPUT_DIR)/images/sdcard.img

CLEAN_PATH := $(shell echo "$$PATH" | tr ':' '\n' | grep -v ' ' | paste -sd:)

HOST ?= $(BOARD)

.PHONY: all help rbf server-cross bundles status push-v1 push-v2 \
        test-corrupt-transfer test-bad-bitstream test-bad-app \
        test buildroot-download buildroot-config buildroot sdcard flash clean

all: rbf buildroot

help:
	@echo "Targets:"
	@echo "  test                        — Run unit & fault-injection tests (no board required)"
	@echo "  bundles                     — Cross-compile v1/v2/crash apps and build OTA bundles"
	@echo "  status HOST=<ip>            — Query active/standby slots on the board"
	@echo "  push-v2 HOST=<ip>           — Part 1: Live network update to v2.0.0 (LED=0xA5)"
	@echo "  push-v1 HOST=<ip>           — Part 1: Live network update to v1.0.0 (LED=0x0F)"
	@echo "  test-corrupt-transfer HOST=<ip> — Part 3A: Test SHA-256 corruption rejection"
	@echo "  test-bad-bitstream HOST=<ip>    — Part 3B: Test bad .rbf automatic rollback"
	@echo "  test-bad-app HOST=<ip>          — Part 3C: Test crashing app automatic rollback"
	@echo "  buildroot                   — Build full Linux SD card image ($(SDCARD_IMG))"
	@echo "  flash SDCARD=/dev/sdX       — Write sdcard.img to physical SD card"
	@echo "  clean                       — Remove generated artifacts"

# ── FPGA bitstream ───────────────────────────────────────────────────────────
rbf: $(RBF_FILE)

$(PHASE6_RBF):
	$(MAKE) -C $(REPO_ROOT)/10_linux_led rbf

$(RBF_FILE): $(PHASE6_RBF)
	@cp "$(PHASE6_RBF)" "$@"
	@echo "FPGA bitstream: $@"

# ── Cross-compile ARM application variants (v1, v2, crash) ──────────────────
server-cross:
	@if command -v arm-linux-gnueabihf-gcc >/dev/null 2>&1; then \
	  $(MAKE) -C software/led_server CC=arm-linux-gnueabihf-gcc all variants; \
	else \
	  $(call SWTOOL,$(MAKE) -C software/led_server CC=arm-linux-gnueabihf-gcc all variants); \
	fi

# ── Build OTA Update Bundles (.tar.gz) ──────────────────────────────────────
bundles: rbf server-cross
	@mkdir -p $(BUILD_DIR)
	python3 client/pack_bundle.py \
	    --rbf $(RBF_FILE) \
	    --app software/led_server/led_server_v1 \
	    --version 1.0.0 \
	    --description "Golden v1.0.0 firmware (startup LED=0x0F)" \
	    --output $(BUILD_DIR)/fw_v1.0.0.tar.gz
	python3 client/pack_bundle.py \
	    --rbf $(RBF_FILE) \
	    --app software/led_server/led_server_v2 \
	    --version 2.0.0 \
	    --description "Updated v2.0.0 firmware (startup LED=0xA5)" \
	    --output $(BUILD_DIR)/fw_v2.0.0.tar.gz
	python3 client/pack_bundle.py \
	    --rbf $(RBF_FILE) \
	    --app software/led_server/led_server_v2 \
	    --version 2.0.0 \
	    --description "Fault A: Corrupted network payload" \
	    --corrupt-payload \
	    --output $(BUILD_DIR)/fw_corrupt.tar.gz
	python3 client/pack_bundle.py \
	    --rbf $(RBF_FILE) \
	    --app software/led_server/led_server_v2 \
	    --version 2.0.0 \
	    --description "Fault B: Invalid/uncompressed FPGA bitstream" \
	    --bad-rbf \
	    --output $(BUILD_DIR)/fw_bad_rbf.tar.gz
	python3 client/pack_bundle.py \
	    --rbf $(RBF_FILE) \
	    --app software/led_server/led_server_crash \
	    --version 2.0.0-broken \
	    --description "Fault C: Crashing application binary" \
	    --output $(BUILD_DIR)/fw_crash_app.tar.gz

# ── Over-The-Air Network Operations ──────────────────────────────────────────
define REQUIRE_HOST
	@if [ -z "$(HOST)" ]; then \
	  echo "Error: Set HOST=<board-ip>, e.g.: make $@ HOST=192.168.1.100" >&2; \
	  exit 1; \
	fi
endef

status:
	$(REQUIRE_HOST)
	python3 client/ota_client.py --host '$(HOST)' status

push-v1: bundles
	$(REQUIRE_HOST)
	python3 client/ota_client.py --host '$(HOST)' push $(BUILD_DIR)/fw_v1.0.0.tar.gz

push-v2: bundles
	$(REQUIRE_HOST)
	python3 client/ota_client.py --host '$(HOST)' push $(BUILD_DIR)/fw_v2.0.0.tar.gz

test-corrupt-transfer: bundles
	$(REQUIRE_HOST)
	python3 client/ota_client.py --host '$(HOST)' push $(BUILD_DIR)/fw_corrupt.tar.gz \
	    --expect-status rejected

test-bad-bitstream: bundles
	$(REQUIRE_HOST)
	python3 client/ota_client.py --host '$(HOST)' push $(BUILD_DIR)/fw_bad_rbf.tar.gz \
	    --expect-status rolled_back

test-bad-app: bundles
	$(REQUIRE_HOST)
	python3 client/ota_client.py --host '$(HOST)' push $(BUILD_DIR)/fw_crash_app.tar.gz \
	    --expect-status rolled_back

# ── Local End-to-End Unit & Rollback Tests (No Board Required) ───────────────
test:
	python3 daemon/test_ota_daemon.py

# ── Buildroot SD Card Image Build ────────────────────────────────────────────
buildroot-download: $(BUILDROOT_DIR)/Makefile

$(BUILDROOT_DIR)/Makefile:
	wget -q --show-progress -O $(BUILDROOT_TARBALL) $(BUILDROOT_URL)
	tar xf $(BUILDROOT_TARBALL)
	rm -f $(BUILDROOT_TARBALL)

buildroot-config: $(BUILDROOT_DIR)/Makefile
	PATH="$(CLEAN_PATH)" $(MAKE) -C $(BUILDROOT_DIR) \
	    BR2_EXTERNAL=$(BR2_EXTERNAL_DIR) de10_nano_defconfig

buildroot: rbf buildroot-config
	PATH="$(CLEAN_PATH)" $(MAKE) -C $(BUILDROOT_DIR) \
	    fpga-led-dirclean fpga-mgr-load-dirclean led-server-dirclean
	PATH="$(CLEAN_PATH)" $(MAKE) -C $(BUILDROOT_DIR)

sdcard: buildroot

SDCARD ?=
flash: $(SDCARD_IMG)
ifndef SDCARD
	$(error Set SDCARD=/dev/sdX, e.g.: make flash SDCARD=/dev/sdb)
endif
	sudo dd if=$(SDCARD_IMG) of=$(SDCARD) bs=4M status=progress conv=fsync

clean:
	rm -f $(RBF_FILE)
	rm -rf $(BUILD_DIR)
	$(MAKE) -C software/led_server clean 2>/dev/null || true
	rm -rf buildroot-*

# Makefile for 17_opcua_hps_led — Phase 11: Industrial IIoT with OPC UA
#
# Exposes the DE10-Nano FPGA LED PIO peripheral (at LWHPS2FPGA 0xFF200000)
# as a self-describing, browsable OPC UA Address Space on TCP port 4840
# (opc.tcp://) and streams cyclic telemetry over OPC UA Part 14 UADP PubSub
# on UDP port 4841.
#
# Usage (from this directory):
#   make test                   — Compile & run automated OPC UA tests (no board required)
#   make compare                — Print wire-level comparison against 11_ethernet_hps_led
#   make server-host            — Build led_opcua_server for local host (--sim mode)
#   make server-cross           — Cross-compile led_opcua_server for ARM Cortex-A9
#   make browse HOST=<ip>       — Discover the DE10-Nano Address Space over opc.tcp
#   make read HOST=<ip>         — Read FPGA.LedPattern over opc.tcp
#   make write HOST=<ip> VAL=0xA5 — Write FPGA.LedPattern over opc.tcp
#   make call HOST=<ip>         — Invoke FPGA.InvertPattern() RPC method over opc.tcp
#   make subscribe HOST=<ip>    — Subscribe to Report-by-Exception DataChangeNotifications
#   make buildroot              — Build complete bootable SD card image (sdcard.img)
#   make flash SDCARD=/dev/sdX  — Write sdcard.img to physical microSD card
#   make clean                  — Remove generated artifacts

PROJECT_DIR   := $(CURDIR)
REPO_ROOT     := $(realpath ..)
PROJECT_NAME  = 17_opcua_hps_led

QUARTUS_SUPPORTED ?= 23.1 25.1
NEEDS_SOFTWARE_TOOLS ?= 1

include ../common/make/quartus-version.mk

# ── FPGA bitstream (reused unchanged from Phase 6 / 10_linux_led) ────────────
PHASE6_RBF    := $(REPO_ROOT)/10_linux_led/de10_nano.rbf
RBF_FILE      := $(PROJECT_DIR)/de10_nano.rbf

# ── Buildroot ────────────────────────────────────────────────────────────────
BUILDROOT_VERSION := 2024.11.1
BUILDROOT_TARBALL := buildroot-$(BUILDROOT_VERSION).tar.xz
BUILDROOT_URL     := https://buildroot.org/downloads/$(BUILDROOT_TARBALL)
BUILDROOT_DIR     := $(PROJECT_DIR)/buildroot-$(BUILDROOT_VERSION)
BR2_EXTERNAL_DIR  := $(PROJECT_DIR)/br2-external
BR2_OUTPUT_DIR    := $(BUILDROOT_DIR)/output
SDCARD_IMG        := $(BR2_OUTPUT_DIR)/images/sdcard.img

CLEAN_PATH := $(shell echo "$$PATH" | tr ':' '\n' | grep -v ' ' | paste -sd:)

HOST ?= $(BOARD)
PORT ?= 4840
NODE ?= fpga.led_pattern
VAL  ?= 0xA5

.PHONY: all help rbf server-host server-cross test compare \
        browse read write call subscribe pubsub-listen \
        buildroot-download buildroot-config buildroot sdcard flash clean

all: rbf buildroot

help:
	@echo "Targets (17_opcua_hps_led — Phase 11: Industrial IIoT with OPC UA):"
	@echo "  test                        — Run automated unit & integration tests (no board required)"
	@echo "  compare                     — Compare wire protocols: 11_ethernet_hps_led vs 17_opcua_hps_led"
	@echo "  server-host                 — Compile led_opcua_server for local PC (--sim mode)"
	@echo "  server-cross                — Cross-compile led_opcua_server for ARM Cortex-A9"
	@echo "  browse HOST=<ip>            — Browse the DE10-Nano OPC UA Address Space (port $(PORT))"
	@echo "  read HOST=<ip> [NODE=...]   — Read an OPC UA Variable node (default: $(NODE))"
	@echo "  write HOST=<ip> [VAL=0xA5]  — Write an OPC UA Variable node"
	@echo "  call HOST=<ip>              — Invoke the fpga.invert_pattern Method node"
	@echo "  subscribe HOST=<ip>         — Subscribe to DataChangeNotifications on $(NODE)"
	@echo "  pubsub-listen               — Listen for OPC UA Part 14 UADP UDP frames (port 4841)"
	@echo "  rbf                         — Copy/verify FPGA bitstream from Phase 6 (10_linux_led)"
	@echo "  buildroot                   — Build full Linux SD card image ($(SDCARD_IMG))"
	@echo "  flash SDCARD=/dev/sdX       — Write sdcard.img to physical SD card"
	@echo "  clean                       — Remove generated files"

# ── FPGA bitstream ───────────────────────────────────────────────────────────
rbf: $(RBF_FILE)

$(PHASE6_RBF):
	@echo "═══════════════════════════════════════════════════════════════"
	@echo "  Building missing dependency: 10_linux_led .rbf bitstream"
	@echo "═══════════════════════════════════════════════════════════════"
	$(MAKE) -C $(REPO_ROOT)/10_linux_led rbf

$(RBF_FILE): $(PHASE6_RBF)
	@echo "Copying FPGA bitstream from Phase 6..."
	@cp "$(PHASE6_RBF)" "$@"
	@echo "FPGA bitstream: $@"

# ── Host & Cross-Compilation of led_opcua_server ─────────────────────────────
server-host:
	$(MAKE) -C software/opcua_server sim

server-cross:
	@if command -v arm-linux-gnueabihf-gcc >/dev/null 2>&1; then \
	  $(MAKE) -B -C software/opcua_server CC=arm-linux-gnueabihf-gcc all; \
	else \
	  $(call SWTOOL,$(MAKE) -B -C software/opcua_server CC=arm-linux-gnueabihf-gcc all); \
	fi
	@echo "Binary: software/opcua_server/led_opcua_server"
	@echo "Deploy: scp software/opcua_server/led_opcua_server root@<board-ip>:/usr/bin/"

# ── Automated Tests & Wire Protocol Comparison (No Board Required) ───────────
test: server-host
	python3 client/test_opcua.py

compare:
	python3 client/compare_protocols.py

# ── Live Board / Sim CLI Shortcuts ───────────────────────────────────────────
define REQUIRE_HOST
	@if [ -z "$(HOST)" ]; then \
	  echo "Error: Set HOST=<board-ip>, e.g.: make $@ HOST=192.168.1.100" >&2; \
	  exit 1; \
	fi
endef

browse:
	$(REQUIRE_HOST)
	python3 client/opcua_client.py --host '$(HOST)' --port $(PORT) browse

read:
	$(REQUIRE_HOST)
	python3 client/opcua_client.py --host '$(HOST)' --port $(PORT) read --node '$(NODE)'

write:
	$(REQUIRE_HOST)
	python3 client/opcua_client.py --host '$(HOST)' --port $(PORT) write --node '$(NODE)' --value '$(VAL)'

call:
	$(REQUIRE_HOST)
	python3 client/opcua_client.py --host '$(HOST)' --port $(PORT) call --method fpga.invert_pattern

subscribe:
	$(REQUIRE_HOST)
	python3 client/opcua_client.py --host '$(HOST)' --port $(PORT) subscribe --node '$(NODE)'

pubsub-listen:
	python3 client/opcua_client.py pubsub-listen

# ── Buildroot SD Card Image Build ────────────────────────────────────────────
buildroot-download: $(BUILDROOT_DIR)/Makefile

$(BUILDROOT_DIR)/Makefile:
	@echo "Downloading Buildroot $(BUILDROOT_VERSION)..."
	wget -q --show-progress -O $(BUILDROOT_TARBALL) $(BUILDROOT_URL)
	tar xf $(BUILDROOT_TARBALL)
	rm -f $(BUILDROOT_TARBALL)

buildroot-config: $(BUILDROOT_DIR)/Makefile
	PATH="$(CLEAN_PATH)" $(MAKE) -C $(BUILDROOT_DIR) \
	    BR2_EXTERNAL=$(BR2_EXTERNAL_DIR) de10_nano_defconfig

buildroot: rbf buildroot-config
	PATH="$(CLEAN_PATH)" $(MAKE) -C $(BUILDROOT_DIR) \
	    fpga-led-dirclean fpga-mgr-load-dirclean led-opcua-server-dirclean
	PATH="$(CLEAN_PATH)" $(MAKE) -C $(BUILDROOT_DIR)
	@echo ""
	@echo "═══════════════════════════════════════════════════════════════════"
	@echo "  Build complete! SD card image: $(SDCARD_IMG)"
	@echo "  Browse Address Space: make browse HOST=<board-ip>"
	@echo "═══════════════════════════════════════════════════════════════════"

sdcard: buildroot

SDCARD ?=
flash: $(SDCARD_IMG)
ifndef SDCARD
	$(error Set SDCARD=/dev/sdX, e.g.: make flash SDCARD=/dev/sdb)
endif
	sudo dd if=$(SDCARD_IMG) of=$(SDCARD) bs=4M status=progress conv=fsync

clean:
	rm -f $(RBF_FILE)
	$(MAKE) -C software/opcua_server clean 2>/dev/null || true
	rm -rf buildroot-* client/__pycache__
	@echo "Cleaned."

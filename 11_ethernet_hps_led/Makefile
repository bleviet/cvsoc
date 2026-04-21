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

# ── nanopb generator (used by 'make proto') ───────────────────────────────────
NANOPB_GEN := $(shell python3 -c \
    "import nanopb, os; print(os.path.join(os.path.dirname(nanopb.__file__), \
    'generator', 'nanopb_generator.py'))" 2>/dev/null)

NANOPB_C_DIR := $(shell python3 -c \
    "import nanopb, os; print(os.path.join(os.path.dirname(nanopb.__file__), \
    'generator', 'proto'))" 2>/dev/null)

.PHONY: all rbf buildroot-download buildroot-config buildroot sdcard \
        proto server-cross server-pb-cross test test-pb \
        dashboard test-dashboard flash clean help

all: rbf buildroot

help:
	@echo "Targets:"
	@echo "  all              — Build everything (rbf + Linux SD card image)"
	@echo "  rbf              — Copy/verify FPGA bitstream from Phase 6"
	@echo "  buildroot        — Full Buildroot build (kernel, U-Boot, rootfs, SD card)"
	@echo "  proto            — Regenerate C/Python stubs from proto/led_command.proto"
	@echo "  server-cross     — ARM cross-compile of led_server  (raw protocol, port 5005)"
	@echo "  server-pb-cross  — ARM cross-compile of led_server_pb (protobuf, port 5006)"
	@echo "  dashboard        — Install Python deps and start the Phase 7.6 web dashboard"
	@echo "  test             — Run Phase 7 Python unit tests (no board required)"
	@echo "  test-pb          — Run Phase 7.5 protobuf unit tests (no board required)"
	@echo "  test-dashboard   — Run Phase 7.6 gateway unit tests (no board required)"
	@echo "  flash            — Write sdcard.img to SD card (set SDCARD=/dev/sdX)"
	@echo "  clean            — Remove generated files"
	@echo ""
	@echo "Output: $(SDCARD_IMG)"
	@echo ""
	@echo "Phase 7 PC client (raw protocol, port 5005):"
	@echo "  python3 client/send_led_pattern.py --host <board-ip> --pattern 0xA5"
	@echo ""
	@echo "Phase 7.5 PC client (protobuf, port 5006):"
	@echo "  python3 client/send_led_pattern_pb.py --host <board-ip> --pattern 0xA5"
	@echo ""
	@echo "Phase 7.6 web dashboard:"
	@echo "  make dashboard BOARD=<board-ip>"
	@echo "  # Then open http://localhost:8080 in a browser"

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

# ── Standalone cross-compile (Phase 7: raw protocol) ─────────────────────────
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

# ── Standalone cross-compile (Phase 7.5: protobuf server) ────────────────────
# Cross-compiles led_server_pb using the nanopb runtime bundled in
# software/led_server/nanopb/ (copied from nanopb-0.4.9.1).
# Uses Docker (same cross-compiler as server-cross).
server-pb-cross:
	docker run --rm \
	    -v $(REPO_ROOT):/work \
	    $(DOCKER_IMAGE) \
	    arm-linux-gnueabihf-gcc -O2 -Wall -Wextra \
	        -I/work/11_ethernet_hps_led/software/led_server/nanopb \
	        -o /work/11_ethernet_hps_led/software/led_server/led_server_pb \
	        /work/11_ethernet_hps_led/software/led_server/led_server_pb.c \
	        /work/11_ethernet_hps_led/software/led_server/led_command.pb.c \
	        /work/11_ethernet_hps_led/software/led_server/nanopb/pb_encode.c \
	        /work/11_ethernet_hps_led/software/led_server/nanopb/pb_decode.c \
	        /work/11_ethernet_hps_led/software/led_server/nanopb/pb_common.c
	@echo ""
	@echo "Binary: software/led_server/led_server_pb"
	@echo "Deploy to board (replace \$$BOARD with your board address):"
	@echo "  scp software/led_server/led_server_pb root@\$$BOARD:/usr/bin/"
	@echo "  ssh root@\$$BOARD 'chmod +x /usr/bin/led_server_pb && led_server_pb &'"

# ── Regenerate protobuf stubs from proto/led_command.proto ───────────────────
# Requires: pip install nanopb grpcio-tools
proto:
	@echo "Generating nanopb C stubs..."
	python3 $(NANOPB_GEN) $(PROJECT_DIR)/proto/led_command.proto \
	    -D $(PROJECT_DIR)/software/led_server/ 2>&1 | grep -v "^$$"
	@# Generator outputs into a 'proto/' subdir — flatten to led_server/
	@if [ -f $(PROJECT_DIR)/software/led_server/proto/led_command.pb.c ]; then \
	    mv $(PROJECT_DIR)/software/led_server/proto/led_command.pb.c \
	       $(PROJECT_DIR)/software/led_server/led_command.pb.c; \
	    mv $(PROJECT_DIR)/software/led_server/proto/led_command.pb.h \
	       $(PROJECT_DIR)/software/led_server/led_command.pb.h; \
	    rmdir $(PROJECT_DIR)/software/led_server/proto 2>/dev/null || true; \
	    sed -i 's|#include "proto/led_command.pb.h"|#include "led_command.pb.h"|' \
	        $(PROJECT_DIR)/software/led_server/led_command.pb.c; \
	fi
	@echo "Generating Python stubs..."
	python3 -m grpc_tools.protoc \
	    --proto_path=$(PROJECT_DIR)/proto \
	    --python_out=$(PROJECT_DIR)/client/ \
	    $(PROJECT_DIR)/proto/led_command.proto
	@echo "Proto generation complete."
	@echo "  software/led_server/led_command.pb.c"
	@echo "  software/led_server/led_command.pb.h"
	@echo "  client/led_command_pb2.py"

# ── Python unit tests ─────────────────────────────────────────────────────────
test:
	cd client && python3 test_protocol.py

test-pb:
	cd client && python3 test_protocol_pb.py

# ── Phase 7.6: Web Dashboard ─────────────────────────────────────────────────
# Installs Python dependencies and starts the asyncio gateway that serves
# dashboard.html on http://localhost:8080 and bridges WebSocket ↔ UDP.
#
# Usage:
#   make dashboard BOARD=192.168.1.100
#   make dashboard BOARD=fe80::2833:8aff:fe95:cb3d%eth0
BOARD ?=

dashboard:
ifndef BOARD
	$(error Set BOARD=<board-ip>, e.g.: make dashboard BOARD=192.168.1.100)
endif
	pip3 install --quiet -r $(PROJECT_DIR)/dashboard/requirements.txt
	@echo ""
	@echo "═══════════════════════════════════════════════════════════════════"
	@echo "  Phase 7.6 — LED Web Dashboard"
	@echo "  Board:     $(BOARD)"
	@echo "  Dashboard: http://localhost:8080"
	@echo "  Press Ctrl+C to stop the gateway."
	@echo "═══════════════════════════════════════════════════════════════════"
	@echo ""
	python3 $(PROJECT_DIR)/dashboard/gateway.py --host '$(BOARD)'

# ── Phase 7.6: gateway unit tests (no board required) ────────────────────────
test-dashboard:
	cd $(PROJECT_DIR)/dashboard && python3 test_gateway.py

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

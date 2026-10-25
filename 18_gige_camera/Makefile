# Makefile for 18_gige_camera — DE10-Nano + D5M as a GigE Vision camera
#
# Product plan: docs/plans/2026-10-25-gige-vision-camera.md
#
# Milestone M0 (throughput spike) targets, run from this directory:
#   make spike-host              — Build gvsp_blast + gvsp_sink for this PC
#   make spike-cross             — Static ARM build of gvsp_blast + eth_pause for the DE10-Nano
#   make test                    — Loopback self-test of the spike tools (no board)
#   make deploy HOST=<ip>        — Copy gvsp_blast + eth_pause to the board (/tmp)
#   make m0-probe-mtu HOST=<ip>  — Find the largest MTU the board's EMAC accepts
#   make m0-sweep HOST=<ip>      — Run the payload x batch sweep, write results/
#   make clean                   — Remove generated artifacts
#
# The board can run any SD-card image with dropbear (11_ethernet_hps_led or
# 17_opcua_hps_led); M0 needs no FPGA design. HOST may be an ssh alias, and
# SINK_HOST runs the sink on another machine (e.g. the jump host the board
# hangs off): see doc/README.md.

PROJECT_DIR  := $(CURDIR)
REPO_ROOT    := $(realpath ..)
PROJECT_NAME  = 18_gige_camera

QUARTUS_SUPPORTED ?= 23.1 25.1
NEEDS_SOFTWARE_TOOLS ?= 1

include ../common/make/quartus-version.mk

SPIKE_DIR := software/spike

HOST     ?= $(BOARD)
HPS_USER ?= root
DURATION ?= 10
SIZES    ?= 1472
BATCHES  ?= 1,8,32,64
SINK_HOST ?=
SINK_BIN  ?=

.PHONY: all help spike-host spike-cross test deploy m0-probe-mtu m0-sweep clean

all: spike-host spike-cross

help:
	@echo "Targets (18_gige_camera — M0 throughput spike):"
	@echo "  spike-host                  — Build gvsp_blast + gvsp_sink for this PC"
	@echo "  spike-cross                 — Static ARM build of gvsp_blast + eth_pause (cvsoc/tools container)"
	@echo "  test                        — Loopback self-test of the spike tools (no board)"
	@echo "  deploy HOST=<ip>            — scp gvsp_blast + eth_pause to $(HPS_USER)@<ip>:/tmp/"
	@echo "  m0-probe-mtu HOST=<ip>      — Report the largest MTU the board's eth0 accepts"
	@echo "  m0-sweep HOST=<ip> [SIZES=1472,3768] [BATCHES=1,8,32,64] [DURATION=10] [DEST_IP=<pc-ip>]"
	@echo "           [SINK_HOST=<ssh-host> SINK_BIN=<path-on-it>]"
	@echo "                              — Sweep payload size x sendmmsg batch, save results/"
	@echo "  clean                       — Remove generated files"

define REQUIRE_HOST
	@if [ -z "$(HOST)" ]; then \
	  echo "Error: Set HOST=<board-ip>, e.g.: make $@ HOST=192.168.1.100" >&2; \
	  exit 1; \
	fi
endef

# ── Spike tools ──────────────────────────────────────────────────────────────
spike-host:
	$(MAKE) -C $(SPIKE_DIR)

spike-cross:
	$(call SWTOOL,$(MAKE) -C $(SPIKE_DIR) CC=arm-linux-gnueabihf-gcc OUT=build/arm LDFLAGS=-static)
	@echo "Binaries: $(SPIKE_DIR)/build/arm/gvsp_blast $(SPIKE_DIR)/build/arm/eth_pause"

test: spike-host
	python3 client/test_spike.py

# ── Board runs ───────────────────────────────────────────────────────────────
deploy: spike-cross
	$(REQUIRE_HOST)
	scp $(SPIKE_DIR)/build/arm/gvsp_blast $(SPIKE_DIR)/build/arm/eth_pause \
	    $(HPS_USER)@$(HOST):/tmp/

m0-probe-mtu:
	$(REQUIRE_HOST)
	python3 client/m0_sweep.py --host '$(HOST)' --user '$(HPS_USER)' --probe-mtu

m0-sweep: spike-host deploy
	python3 client/m0_sweep.py --host '$(HOST)' --user '$(HPS_USER)' \
	    --sizes '$(SIZES)' --batches '$(BATCHES)' --duration $(DURATION) \
	    $(if $(DEST_IP),--dest-ip '$(DEST_IP)') \
	    $(if $(SINK_HOST),--sink-host '$(SINK_HOST)' --sink-bin '$(SINK_BIN)')

clean:
	$(MAKE) -C $(SPIKE_DIR) clean
	rm -rf client/__pycache__
	@echo "Cleaned."

# Makefile for 18_gige_camera — DE10-Nano + D5M as a GigE Vision camera
#
# Product plan: docs/plans/2026-10-25-gige-vision-camera.md
#
# Targets, run from this directory (make help lists them all):
#   make camd-host               — Build camd (GigE Vision daemon) for this PC
#   make camd-cross              — Static ARM build of camd for the DE10-Nano
#   make test                    — camd protocol tests + spike self-test (no board)
#   make sim                     — cocotb RTL tests of cam_tpg_writer (GHDL)
#   make fpga                    — Platform Designer + Quartus compile + compressed .rbf
#   make load-fpga HOST=<ip>     — Load quartus/de10_nano.rbf on the board (fpga_load)
#   make run-camd HOST=<ip>      — Start camd --fpga on the board
#
# Milestone M0 (throughput spike):
#   make spike-host              — Build gvsp_blast + gvsp_sink for this PC
#   make spike-cross             — Static ARM build of gvsp_blast + eth_pause for the DE10-Nano
#   make deploy HOST=<ip>        — Copy camd, gvsp_blast and eth_pause to the board (/tmp)
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
CAMD_DIR  := software/camd

HOST     ?= $(BOARD)
HPS_USER ?= root
DURATION ?= 10
SIZES    ?= 1472
BATCHES  ?= 1,8,32,64
SINK_HOST ?=
SINK_BIN  ?=

.PHONY: all help camd-host camd-cross spike-host spike-cross test sim fpga \
        load-fpga run-camd deploy m0-probe-mtu m0-sweep clean

all: camd-host camd-cross spike-host spike-cross

help:
	@echo "Targets (18_gige_camera — GigE Vision camera):"
	@echo "  camd-host                   — Build camd for this PC (run with --sim)"
	@echo "  camd-cross                  — Static ARM build of camd (cvsoc/tools container)"
	@echo "  test                        — camd protocol tests + spike loopback test (no board)"
	@echo "  sim                         — cocotb RTL tests of cam_tpg_writer (GHDL)"
	@echo "  fpga                        — Platform Designer + Quartus + compressed .rbf"
	@echo "  load-fpga HOST=<ip>         — Load the .rbf on the board with fpga_load"
	@echo "  run-camd HOST=<ip>          — Deploy and start camd --fpga on the board"
	@echo ""
	@echo "M0 throughput spike:"
	@echo "  spike-host                  — Build gvsp_blast + gvsp_sink for this PC"
	@echo "  spike-cross                 — Static ARM build of gvsp_blast + eth_pause (cvsoc/tools container)"
	@echo "  deploy HOST=<ip>            — scp camd, gvsp_blast, eth_pause to $(HPS_USER)@<ip>:/tmp/"
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

# ── camd ─────────────────────────────────────────────────────────────────────
camd-host:
	$(MAKE) -C $(CAMD_DIR)

camd-cross:
	$(call SWTOOL,$(MAKE) -C $(CAMD_DIR) CC=arm-linux-gnueabihf-gcc OUT=build/arm LDFLAGS=-static)
	@echo "Binary: $(CAMD_DIR)/build/arm/camd"

# ── FPGA design ──────────────────────────────────────────────────────────────
sim:
	python3 sim/run.py

fpga:
	$(MAKE) -C quartus all

# The board image's fpga_load module reprograms the FPGA from Linux (it gates
# the AXI bridges during reconfiguration). led_server is stopped first because
# it writes LW-bridge offset 0, which is the camera's ID register here.
load-fpga:
	$(REQUIRE_HOST)
	scp quartus/de10_nano.rbf $(HPS_USER)@$(HOST):/lib/firmware/cam.rbf
	ssh $(HPS_USER)@$(HOST) '/etc/init.d/S40led_server stop >/dev/null 2>&1; \
	    killall camd 2>/dev/null; rmmod fpga_load 2>/dev/null; \
	    modprobe fpga_load firmware=cam.rbf && dmesg | grep fpga_load | tail -2 && \
	    echo "camera ID: $$(devmem 0xFF200000 32)"'

run-camd: camd-cross
	$(REQUIRE_HOST)
	ssh $(HPS_USER)@$(HOST) 'killall camd 2>/dev/null; sleep 0.5; true'
	scp $(CAMD_DIR)/build/arm/camd $(HPS_USER)@$(HOST):/tmp/camd
	ssh $(HPS_USER)@$(HOST) 'nohup /tmp/camd --fpga >/tmp/camd.log 2>&1 & sleep 1.5; cat /tmp/camd.log'

# ── Spike tools ──────────────────────────────────────────────────────────────
spike-host:
	$(MAKE) -C $(SPIKE_DIR)

spike-cross:
	$(call SWTOOL,$(MAKE) -C $(SPIKE_DIR) CC=arm-linux-gnueabihf-gcc OUT=build/arm LDFLAGS=-static)
	@echo "Binaries: $(SPIKE_DIR)/build/arm/gvsp_blast $(SPIKE_DIR)/build/arm/eth_pause"

test: camd-host spike-host
	python3 client/test_camd.py
	python3 client/test_spike.py

# ── Board runs ───────────────────────────────────────────────────────────────
deploy: camd-cross spike-cross
	$(REQUIRE_HOST)
	scp $(CAMD_DIR)/build/arm/camd $(SPIKE_DIR)/build/arm/gvsp_blast \
	    $(SPIKE_DIR)/build/arm/eth_pause $(HPS_USER)@$(HOST):/tmp/

m0-probe-mtu:
	$(REQUIRE_HOST)
	python3 client/m0_sweep.py --host '$(HOST)' --user '$(HPS_USER)' --probe-mtu

m0-sweep: spike-host deploy
	python3 client/m0_sweep.py --host '$(HOST)' --user '$(HPS_USER)' \
	    --sizes '$(SIZES)' --batches '$(BATCHES)' --duration $(DURATION) \
	    $(if $(DEST_IP),--dest-ip '$(DEST_IP)') \
	    $(if $(SINK_HOST),--sink-host '$(SINK_HOST)' --sink-bin '$(SINK_BIN)')

clean:
	$(MAKE) -C $(CAMD_DIR) clean
	rm -rf sim/sim_build sim/results.xml sim/__pycache__
	$(MAKE) -C $(SPIKE_DIR) clean
	rm -rf client/__pycache__
	@echo "Cleaned."

# Makefile for the 09_hps_debug Quartus project.
# Orchestrates: Platform Designer → Quartus compile → ARM app build (tools container).
# Adds OpenOCD server and interactive GDB debug targets on top of the
# 07_hps_interrupts flow (the bitstream is copied from 07).
#
# FPGA build steps run via the shared quartus-version.mk fragment (local 25.1
# or cvsoc/quartus:23.1 Docker).  The ARM app is built in cvsoc/tools:1.0.
# openocd/gdb/download-elf stay on the 23.1 container (Altera openocd + gdb).

PROJECT_NAME  = 09_hps_debug
REVISION_NAME = de10_nano

QSYS_TCL     = ../qsys/hps_system.tcl
QSYS_FILE    = ../qsys/hps_system.qsys
QSYS_GEN_DIR = ../qsys/hps_system/synthesis
APP_DIR      = ../software/app
ARM_CC       ?= arm-linux-gnueabihf-gcc

QUARTUS_SUPPORTED ?= 23.1 25.1
QUARTUS_DEFAULT   ?= 23.1
NEEDS_SOFTWARE_TOOLS ?= 1

include ../../common/make/quartus-version.mk

HPS_IP   ?= 192.168.1.100
HPS_USER ?= root

# GDB port for OpenOCD
GDB_PORT ?= 3333

UNAME_SHIM  = $(REPO_ROOT)/common/docker/uname_shim.sh
OPENOCD_CFG = ../scripts/de10_nano_hps.cfg

# Debug/download targets always run in the 23.1 container: only its Altera
# openocd (with the uname shim) supports the DE10-Nano USB-Blaster II, and they
# run via docker regardless of the effective QUARTUS_RUNNER.
DEBUG_IMAGE ?= cvsoc/quartus:23.1

.PHONY: all qsys patch-oct project compile app check_timing \
        program-sof download-elf deploy-elf openocd gdb \
        usb-wsl usb-windows clean

all:
	@echo "═══════════════════════════════════════════════════════════════"
	@echo "  Phase 5 reuses Phase 4 (07_hps_interrupts) hardware"
	@echo "═══════════════════════════════════════════════════════════════"
	$(MAKE) -C $(REPO_ROOT)/07_hps_interrupts/quartus all
	@echo "Copying bitstream from 07_hps_interrupts..."
	$(call QTOOL, \
	  mkdir -p ../software/app && \
	  rm -f $(REVISION_NAME).sof && \
	  cp ../07_hps_interrupts/quartus/de10_nano.sof $(REVISION_NAME).sof)
	$(call SWTOOL,make -C software/app CC=$(ARM_CC))

$(QSYS_FILE): $(QSYS_TCL)
	$(call QTOOL,cd ../qsys && qsys-script --script=hps_system.tcl)

qsys: $(QSYS_FILE)
	$(call QTOOL, \
	  qsys-generate $(QSYS_FILE) --synthesis=VERILOG && \
	  python3 $(QROOT)/09_hps_debug/scripts/patch_oct.py \
	    ../qsys/hps_system/synthesis/submodules/altdq_dqs2_acv_connect_to_hard_phy_cyclonev.sv)

patch-oct:
	$(call QTOOL,python3 $(QROOT)/09_hps_debug/scripts/patch_oct.py \
	  ../qsys/hps_system/synthesis/submodules/altdq_dqs2_acv_connect_to_hard_phy_cyclonev.sv)

# project: create the Quartus project (and compile the qip) from the TCL.
project: $(QSYS_GEN_DIR)/hps_system.qip
	$(call QTOOL,quartus_sh -t de10_nano_project.tcl)

# Two-pass compilation flow required for Cyclone V HPS DDR3 (same as 07_hps_interrupts).
compile:
	$(call QTOOL, \
	  quartus_map --read_settings_files=on --write_settings_files=off \
	    $(PROJECT_NAME) -c $(REVISION_NAME) && \
	  quartus_sta -t $(QSYS_GEN_DIR)/submodules/hps_sdram_p0_pin_assignments.tcl \
	    $(REVISION_NAME) && \
	  quartus_map --read_settings_files=on --write_settings_files=off \
	    $(PROJECT_NAME) -c $(REVISION_NAME) && \
	  quartus_fit --read_settings_files=on --write_settings_files=off \
	    $(PROJECT_NAME) -c $(REVISION_NAME) && \
	  quartus_asm --read_settings_files=on --write_settings_files=off \
	    $(PROJECT_NAME) -c $(REVISION_NAME) && \
	  quartus_sta $(PROJECT_NAME) -c $(REVISION_NAME))

# app: cross-compile the ARM bare-metal application in cvsoc/tools:1.0.
app:
	$(call SWTOOL,make -C software/app CC=$(ARM_CC))

check_timing:
	$(call QTOOL,python3 $(QROOT)/00_led_blinking/scripts/check_timing_slacks.py \
	  $(REVISION_NAME).sta.rpt 2>/dev/null || true)

program-sof: usb-ready $(REVISION_NAME).sof
	$(QUARTUS_PROGRAM)

# ── Download ELF (load and run, no interactive debugging) ────────────────────
download-elf: usb-ready $(APP_DIR)/hps_debug.elf
	docker run --rm --user $$(id -u):$$(id -g) --privileged \
	  -v $(REPO_ROOT):/work \
	  -v $(UNAME_SHIM):/usr/local/bin/uname:ro \
	  -v $(realpath $(OPENOCD_CFG)):/tmp/de10_nano_hps.cfg:ro \
	  $(DEBUG_IMAGE) \
	  bash -c '\
	    /opt/intelFPGA/quartus/bin/jtagd && sleep 2 && \
	    /opt/intelFPGA/quartus/bin/openocd \
	      -f /tmp/de10_nano_hps.cfg \
	      -c "init" \
	      -c "halt" \
	      -c "load_image /work/09_hps_debug/software/app/hps_debug.bin 0xFFFF0000 bin" \
	      -c "resume 0xFFFF0000" \
	      -c "shutdown"; \
	    kill $$(pgrep jtagd) 2>/dev/null || true'
	@echo "Done: HPS debug application is running."

deploy-elf: $(APP_DIR)/hps_debug.elf
	scp $(APP_DIR)/hps_debug.elf $(APP_DIR)/hps_debug.bin \
	    $(HPS_USER)@$(HPS_IP):/tmp/
	@echo "ELF + binary copied to $(HPS_USER)@$(HPS_IP):/tmp/"

# ── OpenOCD server (start OpenOCD as GDB server, keep running) ───────────────
#
# Run this in one terminal, then run 'make gdb' in a second terminal.
# OpenOCD resets the Cortex-A9, waits for a GDB connection on port $(GDB_PORT).
#
# NOTE: Exposes port $(GDB_PORT) from the Docker container to localhost so
#       the second 'make gdb' container can connect via --network host.
openocd: usb-ready
	docker run --rm --user $$(id -u):$$(id -g) -it --privileged \
	  -p $(GDB_PORT):$(GDB_PORT) \
	  -p 4444:4444 \
	  -v $(REPO_ROOT):/work \
	  -v $(UNAME_SHIM):/usr/local/bin/uname:ro \
	  -v $(realpath $(OPENOCD_CFG)):/tmp/de10_nano_hps.cfg:ro \
	  $(DEBUG_IMAGE) \
	  bash -c '\
	    /opt/intelFPGA/quartus/bin/jtagd && sleep 2 && \
	    /opt/intelFPGA/quartus/bin/openocd \
	      -f /tmp/de10_nano_hps.cfg \
	      -c "init" \
	      -c "halt"; \
	    kill $$(pgrep jtagd) 2>/dev/null || true'

# ── Interactive GDB session ───────────────────────────────────────────────────
#
# Connects arm-none-eabi-gdb to the running OpenOCD server.
# The GDB init script connects to OpenOCD, sets breakpoints, and continues.
#
# Usage:
#   Terminal 1: make openocd
#   Terminal 2: make gdb
gdb: $(APP_DIR)/hps_debug.elf
	docker run --rm --user $$(id -u):$$(id -g) -it \
	  --network host \
	  -v $(REPO_ROOT):/work \
	  $(DEBUG_IMAGE) \
	  arm-none-eabi-gdb \
	    -ex "set pagination off" \
	    -x /work/09_hps_debug/scripts/hps_debug.gdb \
	    /work/09_hps_debug/software/app/hps_debug.elf

clean:
	$(call QTOOL, \
	  rm -rf db incremental_db output_files hps_isw_handoff && \
	  rm -rf $(QSYS_FILE) ../qsys/hps_system ../qsys/hps_system.sopcinfo && \
	  make -C $(APP_DIR) clean 2>/dev/null || true && \
	  find . -maxdepth 1 \( -name '*.rpt' -o -name '*.summary' -o -name '*.qsf' \
	    -o -name '*.qpf' -o -name '*.qws' -o -name '*.done' -o -name '*.smsg' \
	    -o -name '*.jdi' -o -name '*.pin' -o -name '*.sld' -o -name '*.sof' \
	    -o -name 'dump.txt' \) -delete)

# Makefile for the 07_hps_interrupts Quartus project.
# Orchestrates: Platform Designer → Quartus compile → ARM app build (tools container).
#
# FPGA build steps run via the shared quartus-version.mk fragment (local 25.1
# or cvsoc/quartus:23.1 Docker).  The ARM app is built in cvsoc/tools:1.0.
# download-elf stays on the 23.1 container's Altera openocd (USB-Blaster II).

PROJECT_NAME  = 07_hps_interrupts
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

UNAME_SHIM  = $(REPO_ROOT)/common/docker/uname_shim.sh
OPENOCD_CFG = ../scripts/de10_nano_hps.cfg

# Debug/download targets always run in the 23.1 container: only its Altera
# openocd (with the uname shim) supports the DE10-Nano USB-Blaster II, and they
# run via docker regardless of the effective QUARTUS_RUNNER.
DEBUG_IMAGE ?= cvsoc/quartus:23.1

.PHONY: all qsys patch-oct project compile app check_timing \
        program-sof download-elf deploy-elf usb-wsl usb-windows clean

all:
	$(call QTOOL, \
	  cd ../qsys && qsys-script --script=hps_system.tcl && \
	  cd ../quartus && \
	  qsys-generate ../qsys/hps_system.qsys --synthesis=VERILOG && \
	  python3 $(QROOT)/07_hps_interrupts/scripts/patch_oct.py \
	    ../qsys/hps_system/synthesis/submodules/altdq_dqs2_acv_connect_to_hard_phy_cyclonev.sv && \
	  quartus_sh -t de10_nano_project.tcl && \
	  quartus_map --read_settings_files=on --write_settings_files=off $(PROJECT_NAME) -c $(REVISION_NAME) && \
	  quartus_sta -t ../qsys/hps_system/synthesis/submodules/hps_sdram_p0_pin_assignments.tcl $(REVISION_NAME) && \
	  quartus_map --read_settings_files=on --write_settings_files=off $(PROJECT_NAME) -c $(REVISION_NAME) && \
	  quartus_fit --read_settings_files=on --write_settings_files=off $(PROJECT_NAME) -c $(REVISION_NAME) && \
	  quartus_asm --read_settings_files=on --write_settings_files=off $(PROJECT_NAME) -c $(REVISION_NAME) && \
	  quartus_sta $(PROJECT_NAME) -c $(REVISION_NAME))
	$(call SWTOOL,make -C software/app CC=$(ARM_CC))

$(QSYS_FILE): $(QSYS_TCL)
	$(call QTOOL,cd ../qsys && qsys-script --script=hps_system.tcl)

qsys: $(QSYS_FILE)
	$(call QTOOL, \
	  qsys-generate $(QSYS_FILE) --synthesis=VERILOG && \
	  python3 $(QROOT)/07_hps_interrupts/scripts/patch_oct.py \
	    ../qsys/hps_system/synthesis/submodules/altdq_dqs2_acv_connect_to_hard_phy_cyclonev.sv)

patch-oct:
	$(call QTOOL,python3 $(QROOT)/07_hps_interrupts/scripts/patch_oct.py \
	  ../qsys/hps_system/synthesis/submodules/altdq_dqs2_acv_connect_to_hard_phy_cyclonev.sv)

# project: create the Quartus project (and compile the qip) from the TCL.
project: $(QSYS_GEN_DIR)/hps_system.qip
	$(call QTOOL,quartus_sh -t de10_nano_project.tcl)

# Two-pass compilation flow required for Cyclone V HPS DDR3 (same as 05_hps_led).
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

# download-elf: run inside the 23.1 container using Altera's openocd with the
# DE10-Nano USB-Blaster II (uname shim required by the OpenOCD support files).
download-elf: usb-ready $(APP_DIR)/hps_interrupts.elf
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
	      -c "load_image /work/07_hps_interrupts/software/app/hps_interrupts.bin 0xFFFF0000 bin" \
	      -c "resume 0xFFFF0000" \
	      -c "shutdown"; \
	    kill $$(pgrep jtagd) 2>/dev/null || true'
	@echo "Done: HPS interrupt application is running."

deploy-elf: $(APP_DIR)/hps_interrupts.elf
	scp $(APP_DIR)/hps_interrupts.elf $(APP_DIR)/hps_interrupts.bin \
	    $(HPS_USER)@$(HPS_IP):/tmp/
	@echo "ELF + binary copied to $(HPS_USER)@$(HPS_IP):/tmp/"

clean:
	$(call QTOOL, \
	  rm -rf db incremental_db output_files hps_isw_handoff && \
	  rm -rf $(QSYS_FILE) ../qsys/hps_system ../qsys/hps_system.sopcinfo && \
	  make -C $(APP_DIR) clean 2>/dev/null || true && \
	  find . -maxdepth 1 \( -name '*.rpt' -o -name '*.summary' -o -name '*.qsf' \
	    -o -name '*.qpf' -o -name '*.qws' -o -name '*.done' -o -name '*.smsg' \
	    -o -name '*.jdi' -o -name '*.pin' -o -name '*.sld' -o -name '*.sof' \
	    -o -name 'dump.txt' \) -delete)

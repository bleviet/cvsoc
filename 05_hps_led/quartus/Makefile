# Makefile for the 05_hps_led Quartus project.
# Orchestrates: Platform Designer → Quartus compile → ARM app build (tools container).
#
# FPGA build steps run via the shared quartus-version.mk fragment, which
# auto-detects a local Quartus install (e.g. 25.1) or falls back to the
# cvsoc/quartus:23.1 Docker container.  The ARM app is built in the separate
# cvsoc/tools:1.0 software-tools container (NEEDS_SOFTWARE_TOOLS=1).

PROJECT_NAME  = 05_hps_led
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

.PHONY: all qsys patch-oct project compile app check_timing \
        program-sof download-elf deploy-elf usb-wsl usb-windows clean

all:
	$(call QTOOL, \
	  cd ../qsys && qsys-script --script=hps_system.tcl && \
	  cd ../quartus && \
	  qsys-generate ../qsys/hps_system.qsys --synthesis=VERILOG && \
	  python3 $(QROOT)/05_hps_led/scripts/patch_oct.py \
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
	  python3 $(QROOT)/05_hps_led/scripts/patch_oct.py \
	    ../qsys/hps_system/synthesis/submodules/altdq_dqs2_acv_connect_to_hard_phy_cyclonev.sv)

patch-oct:
	$(call QTOOL,python3 $(QROOT)/05_hps_led/scripts/patch_oct.py \
	  ../qsys/hps_system/synthesis/submodules/altdq_dqs2_acv_connect_to_hard_phy_cyclonev.sv)

# project: create the Quartus project (and compile the qip) from the TCL.
project: $(QSYS_GEN_DIR)/hps_system.qip
	$(call QTOOL,quartus_sh -t de10_nano_project.tcl)

# Two-pass compilation flow required for Cyclone V HPS DDR3:
#
#  Pass 1  quartus_map   – synthesise the design and build a timing netlist
#  Apply   quartus_sta   – run hps_sdram_p0_pin_assignments.tcl to inject the
#                          DDR3 I/O-standard and OCT-calibration assignments
#                          into the QSF (needs the timing netlist from pass 1)
#  Pass 2  quartus_map   – re-synthesise picking up the new QSF assignments
#          quartus_fit   – place & route (DDR3 OCT errors resolved by pass 1+apply)
#          quartus_asm   – generate the .sof bitstream
#          quartus_sta   – final timing analysis
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

# download-elf: OpenOCD on the host with the DE10-Nano USB-Blaster II config.
OPENOCD_UBLAST_CFG = ../scripts/de10_nano_hps_ublast2.cfg
BLASTER_FW = $(REPO_ROOT)/12_zephyr_led/zephyr/boards/intel/socfpga_std/cyclonev_socdk/support/blaster_6810.hex
APP_BIN    = $(REPO_ROOT)/$(PROJECT_NAME)/software/app/hps_led.bin

download-elf: usb-ready $(APP_DIR)/hps_led.elf
	openocd \
	  -c "set BLASTER_FW $(BLASTER_FW)" \
	  -f $(realpath $(OPENOCD_UBLAST_CFG)) \
	  -c "init" \
	  -c "fpgasoc_prepare_ocram_exec" \
	  -c "load_image $(APP_BIN) 0xFFFF0000 bin" \
	  -c "resume 0xFFFF0000" \
	  -c "shutdown"
	@echo "Done: HPS LED application is running."

deploy-elf: $(APP_DIR)/hps_led.elf
	scp $(APP_DIR)/hps_led.elf $(APP_DIR)/hps_led.bin \
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

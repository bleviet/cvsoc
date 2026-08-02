# Makefile for the 14_ddr3_hps_test Quartus project.
# Orchestrates: Platform Designer → Quartus compile → ARM app build (tools container).
#
# FPGA build steps run via the shared quartus-version.mk fragment (local 25.1
# or cvsoc/quartus:23.1 Docker).  The ARM app is built in cvsoc/tools:1.0.
# download-elf uses the host (apt) openocd with the USB-Blaster II config.

PROJECT_NAME  = 14_ddr3_hps_test
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

program-sof: usb-wsl $(REVISION_NAME).sof
	$(QUARTUS_PROGRAM)

# download-elf: Loads the bare-metal binary into DDR3 via JTAG (host OpenOCD).
#   NOTE: The DDR3 controller must be initialised by U-Boot SPL first.
#   Boot the board to U-Boot (do not boot into Linux), then run this target.
#
#   Uses the standard system openocd with the usb_blaster/ublast2 driver.
#
#   Prerequisites on the WSL2 host:
#     sudo apt-get install openocd          # version 0.11.0 or later
#     # udev rules for USB Blaster II are already installed by the repo setup
OPENOCD_UBLAST_CFG = ../scripts/de10_nano_hps_ublast2.cfg
BLASTER_FW         = $(REPO_ROOT)/12_zephyr_led/zephyr/boards/intel/socfpga_std/cyclonev_socdk/support/blaster_6810.hex
APP_BIN            = $(REPO_ROOT)/$(PROJECT_NAME)/software/app/ddr3_hps_test.bin

download-elf: usb-wsl $(APP_DIR)/ddr3_hps_test.elf
	openocd \
	  -c "set BLASTER_FW $(BLASTER_FW)" \
	  -f $(realpath $(OPENOCD_UBLAST_CFG)) \
	  -c "init" \
	  -c "halt" \
	  -c "load_image $(APP_BIN) 0x00100000 bin" \
	  -c "resume 0x00100000" \
	  -c "shutdown"
	@echo "Done: DDR3 HPS test application is running."

deploy-elf: $(APP_DIR)/ddr3_hps_test.elf
	scp $(APP_DIR)/ddr3_hps_test.elf $(APP_DIR)/ddr3_hps_test.bin \
	    $(HPS_USER)@$(HPS_IP):/tmp/
	@echo "ELF + binary copied to $(HPS_USER)@$(HPS_IP):/tmp/"
	@echo "  To run from U-Boot: fatload / go 0x00100000"

clean:
	$(call QTOOL, \
	  rm -rf db incremental_db output_files hps_isw_handoff && \
	  rm -rf $(QSYS_FILE) ../qsys/hps_system ../qsys/hps_system.sopcinfo && \
	  make -C $(APP_DIR) clean 2>/dev/null || true && \
	  find . -maxdepth 1 \( -name '*.rpt' -o -name '*.summary' -o -name '*.qsf' \
	    -o -name '*.qpf' -o -name '*.qws' -o -name '*.done' -o -name '*.smsg' \
	    -o -name '*.jdi' -o -name '*.pin' -o -name '*.sld' -o -name '*.sof' \
	    -o -name 'dump.txt' \) -delete)

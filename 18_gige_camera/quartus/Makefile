# Makefile for the 18_gige_camera Quartus project (M1 FPGA design).
# Orchestrates: Platform Designer → Quartus compile → compressed .rbf.
#
# FPGA build steps run via the shared quartus-version.mk fragment (local 25.1
# or cvsoc/quartus:23.1 Docker). The .rbf is loaded from Linux on the board
# (see ../doc/README.md), so it must be compressed: the DE10-Nano MSEL setting
# expects compressed bitstreams.

PROJECT_NAME  = 18_gige_camera
REVISION_NAME = de10_nano

QSYS_TCL     = ../qsys/hps_system.tcl
QSYS_FILE    = ../qsys/hps_system.qsys
QSYS_GEN_DIR = ../qsys/hps_system/synthesis
RBF_FILE     = $(REVISION_NAME).rbf

QUARTUS_SUPPORTED ?= 23.1 25.1
QUARTUS_DEFAULT   ?= 23.1
NEEDS_SOFTWARE_TOOLS ?= 0

include ../../common/make/quartus-version.mk

# qsys-generate must find the custom cam_tpg_writer component, whose TCL
# descriptor lives in ../qsys/. The trailing "$$" keeps Platform Designer's
# default search path.
QSYS_SP = --search-path=$(QROOT)/$(PROJECT_NAME)/qsys,$$

# Platform Designer locates Quartus via QUARTUS_ROOTDIR; export it so the
# custom-component search path resolves in both Docker and local installs.
ifeq ($(QUARTUS_HOME),)
QSYS_ENV := export QUARTUS_ROOTDIR=/opt/intelFPGA/quartus &&
else
QSYS_ENV := export QUARTUS_ROOTDIR=$(QUARTUS_HOME)/quartus &&
endif

PATCH_OCT = python3 $(QROOT)/05_hps_led/scripts/patch_oct.py \
              $(QSYS_GEN_DIR)/submodules/altdq_dqs2_acv_connect_to_hard_phy_cyclonev.sv

.PHONY: all qsys project compile rbf check_timing program-sof clean

all: qsys project compile rbf

qsys:
	$(call QTOOL, \
	  $(QSYS_ENV) cd ../qsys && qsys-script --script=hps_system.tcl $(QSYS_SP) && \
	  qsys-generate hps_system.qsys --synthesis=VERILOG $(QSYS_SP) && \
	  cd ../quartus && $(PATCH_OCT))

project:
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

rbf:
	$(call QTOOL, \
	  bash $(QROOT)/10_linux_led/scripts/convert_sof_to_rbf.sh \
	    $(REVISION_NAME).sof $(RBF_FILE))

check_timing:
	$(call QTOOL,python3 $(QROOT)/00_led_blinking/scripts/check_timing_slacks.py \
	  $(REVISION_NAME).sta.rpt 2>/dev/null || true)

program-sof: usb-ready
	$(QUARTUS_PROGRAM)

clean:
	$(call QTOOL, \
	  rm -rf db incremental_db hps_isw_handoff *.qpf *.qsf *.rpt *.summary *.sof *.rbf && \
	  rm -rf $(QSYS_FILE) ../qsys/hps_system ../qsys/hps_system.sopcinfo)

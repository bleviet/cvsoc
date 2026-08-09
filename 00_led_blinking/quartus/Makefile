# Makefile for the 00_led_blinking Quartus project.
# Pure HDL design — no software component. Multi-version Quartus via
# common/make/quartus-version.mk (local 25.1 preferred, Docker 23.1 fallback).

PROJECT_NAME  = 00_led_blinking
REVISION_NAME = de10_nano

QUARTUS_SUPPORTED ?= 23.1 25.1
QUARTUS_DEFAULT   ?= 23.1
NEEDS_SOFTWARE_TOOLS ?= 0

include ../../common/make/quartus-version.mk

.PHONY: all project compile check_timing program-sof usb-wsl usb-windows clean

# all: creates the project and compiles in a single invocation.
all:
	$(call QTOOL, \
	  quartus_sh -t de10_nano_project.tcl && \
	  quartus_sh --flow compile $(PROJECT_NAME) -c $(REVISION_NAME) && \
	  python3 $(QROOT)/00_led_blinking/scripts/check_timing_slacks.py \
	    $(REVISION_NAME).sta.rpt 2>/dev/null || true)

project:
	$(call QTOOL,quartus_sh -t de10_nano_project.tcl)

compile:
	$(call QTOOL,quartus_sh --flow compile $(PROJECT_NAME) -c $(REVISION_NAME))

check_timing:
	$(call QTOOL,python3 $(QROOT)/00_led_blinking/scripts/check_timing_slacks.py \
	  $(REVISION_NAME).sta.rpt 2>/dev/null || true)

program-sof: usb-ready $(REVISION_NAME).sof
	$(QUARTUS_PROGRAM)

clean:
	rm -rf db incremental_db output_files
	rm -f $(foreach ENDING,.rpt .summary .qsf .qpf .qws .done .smsg .jdi .pin .sld .sof dump.txt,$(wildcard *$(ENDING)))

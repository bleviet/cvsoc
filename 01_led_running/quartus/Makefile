# Makefile for the 01_led_running Quartus project.
# Pure HDL design — no software component.
#
# All build steps run inside the cvsoc/quartus:23.1 Docker container.
# Usage:
#   docker run --rm -v <repo_root>:/work cvsoc/quartus:23.1 \
#     bash -c "cd /work/01_led_running/quartus && make all"
#
# Programming and USB management targets (program-sof, usb-wsl, usb-windows)
# are called from the WSL2 host, not from inside Docker.

PROJECT_NAME  = 01_led_running
REVISION_NAME = de10_nano

DOCKER_IMAGE ?= cvsoc/quartus:23.1

# usbipd-win bus ID for the USB-Blaster II.
# Find yours with: usbipd.exe list  (look for "USB-Blaster")
# Override with: make USBIPD_BUSID=<id> program-sof
USBIPD       ?= usbipd.exe
USBIPD_BUSID ?= 2-4

# JTAG chain device index: 1=HPS ARM DAP, 2=Cyclone V FPGA
DEVICE_INDEX ?= 2

REPO_ROOT = $(realpath ../..)

.PHONY: all project compile analysis fit asm sta check_timing \
        program-sof usb-wsl usb-windows clean

all: project compile check_timing

# ── Build targets ─────────────────────────────────────────────────────────────
project:
	quartus_sh -t de10_nano_project.tcl

analysis:
	quartus_map $(PROJECT_NAME) -c $(REVISION_NAME)

fit:
	quartus_fit $(PROJECT_NAME) -c $(REVISION_NAME)

asm:
	quartus_asm $(PROJECT_NAME) -c $(REVISION_NAME)

sta:
	quartus_sta $(PROJECT_NAME) -c $(REVISION_NAME)

compile:
	quartus_sh --flow compile $(PROJECT_NAME) -c $(REVISION_NAME)

check_timing:
	python ../../00_led_blinking/scripts/check_timing_slacks.py $(REVISION_NAME).sta.rpt 2>/dev/null || true

# ── Programming targets ───────────────────────────────────────────────────────
# usb-wsl: Connects the USB-Blaster to WSL2 so Docker can reach it.
usb-wsl:
	@echo "Connecting USB-Blaster (busid $(USBIPD_BUSID)) to WSL2..."
	$(USBIPD) attach --wsl --busid $(USBIPD_BUSID) 2>/dev/null || true
	@sleep 1

# usb-windows: Returns the USB-Blaster to Windows (Windows fallback workflow only).
#   Not needed for the standard WSL2 flow.
#   See docs/howto_windows_wsl2_programmer.md for the Windows programmer workflow.
usb-windows:
	@echo "Detaching USB-Blaster (busid $(USBIPD_BUSID)) from WSL2 → Windows..."
	$(USBIPD) detach --busid $(USBIPD_BUSID) 2>/dev/null || true
	@sleep 1

# program-sof: Programs the FPGA bitstream via JTAG entirely from WSL2.
#   Starts jtagd inside the container then calls quartus_pgm.
#   The USB-Blaster must be connected to WSL2 first (handled by usb-wsl).
program-sof: usb-wsl $(REVISION_NAME).sof
	docker run --rm --privileged \
	  -v /dev/bus/usb:/dev/bus/usb \
	  -v $(REPO_ROOT):/work \
	  $(DOCKER_IMAGE) \
	  bash -c 'jtagd && sleep 2 && \
	    quartus_pgm -m jtag -o "p;/work/$(PROJECT_NAME)/quartus/$(REVISION_NAME).sof@$(DEVICE_INDEX)"'

# ── Clean ─────────────────────────────────────────────────────────────────────
FILE_ENDINGS_FOR_REMOVAL = .rpt .summary .qsf .qpf .qws .done .smsg .jdi .pin .sld .sof dump.txt

clean:
	rm -rf db incremental_db output_files
	rm -f $(foreach ENDING,$(FILE_ENDINGS_FOR_REMOVAL),$(wildcard *$(ENDING)))


# Makefile for the 01_led_running Quartus project.
# Pure HDL design — no software component.
#
# Every build target runs inside the cvsoc/quartus:23.1 Docker container
# automatically. Call any target directly from the WSL2 host:
#
#   make all
#   make compile
#   make program-sof USBIPD_BUSID=2-4

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

REPO_ROOT  = $(realpath ../..)
# Docker run helper: mounts repo at /work, sets working dir to this project.
DOCKER     = docker run --rm -v $(REPO_ROOT):/work $(DOCKER_IMAGE) bash -c
DOCKER_CWD = cd /work/$(PROJECT_NAME)/quartus

.PHONY: all project compile analysis fit asm sta check_timing \
        program-sof usb-wsl usb-windows clean

# all: runs project → compile → check_timing in a single container.
all:
	docker run --rm \
	  -v $(REPO_ROOT):/work \
	  $(DOCKER_IMAGE) \
	  bash -c "$(DOCKER_CWD) && \
	    quartus_sh -t de10_nano_project.tcl && \
	    quartus_sh --flow compile $(PROJECT_NAME) -c $(REVISION_NAME) && \
	    python /work/00_led_blinking/scripts/check_timing_slacks.py \
	      $(REVISION_NAME).sta.rpt 2>/dev/null || true"

# ── Build targets (each spawns its own container) ────────────────────────────
project:
	$(DOCKER) "$(DOCKER_CWD) && quartus_sh -t de10_nano_project.tcl"

analysis:
	$(DOCKER) "$(DOCKER_CWD) && quartus_map $(PROJECT_NAME) -c $(REVISION_NAME)"

fit:
	$(DOCKER) "$(DOCKER_CWD) && quartus_fit $(PROJECT_NAME) -c $(REVISION_NAME)"

asm:
	$(DOCKER) "$(DOCKER_CWD) && quartus_asm $(PROJECT_NAME) -c $(REVISION_NAME)"

sta:
	$(DOCKER) "$(DOCKER_CWD) && quartus_sta $(PROJECT_NAME) -c $(REVISION_NAME)"

compile:
	$(DOCKER) "$(DOCKER_CWD) && quartus_sh --flow compile $(PROJECT_NAME) -c $(REVISION_NAME)"

check_timing:
	$(DOCKER) "$(DOCKER_CWD) && python /work/00_led_blinking/scripts/check_timing_slacks.py \
	  $(REVISION_NAME).sta.rpt 2>/dev/null || true"

# ── Programming targets ───────────────────────────────────────────────────────
# usb-wsl: Connects the USB-Blaster to WSL2 so Docker can reach it.
usb-wsl:
	@echo "Connecting USB-Blaster (busid $(USBIPD_BUSID)) to WSL2..."
	$(USBIPD) attach --wsl --busid $(USBIPD_BUSID) 2>/dev/null || true
	@sleep 1

# usb-windows: Returns the USB-Blaster to Windows (Windows fallback workflow only).
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



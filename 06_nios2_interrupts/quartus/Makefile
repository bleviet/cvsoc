# Makefile for the 06_nios2_interrupts Quartus project.
# Orchestrates: Platform Designer generation → Quartus compile → BSP generation → app build.
#
# Run inside the raetro/quartus Docker container (all tools are in PATH).
# Usage:
#   docker run --rm -v <repo_root>:/work raetro/quartus:23.1 \
#     bash -c "cd /work/06_nios2_interrupts/quartus && make all"

PROJECT_NAME  = 06_nios2_interrupts
REVISION_NAME = de10_nano

QSYS_TCL      = ../qsys/nios2_system.tcl
QSYS_FILE     = ../qsys/nios2_system.qsys
QSYS_GEN_DIR  = ../qsys/nios2_system_gen
SOPCINFO      = ../qsys/nios2_system.sopcinfo

BSP_DIR       = ../software/bsp
APP_DIR       = ../software/app

QUARTUS_PGM  ?= /mnt/c/intelFPGA_lite/23.1std/qprogrammer/bin64/quartus_pgm.exe
DOCKER_IMAGE ?= raetro/quartus:23.1

USBIPD       ?= usbipd.exe
USBIPD_BUSID ?= 2-4

.PHONY: all qsys project compile bsp app clean \
        program-sof download-elf terminal usb-windows usb-wsl

all: qsys project compile bsp app

# ── Step 1: generate the .qsys file from the TCL script ──────────────────────
$(QSYS_FILE): $(QSYS_TCL)
	cd ../qsys && qsys-script --script=nios2_system.tcl

qsys: $(QSYS_FILE)
	qsys-generate $(QSYS_FILE) --synthesis=VHDL --output-directory=$(QSYS_GEN_DIR)

# ── Step 2: create and compile the Quartus project ───────────────────────────
project: $(QSYS_GEN_DIR)/synthesis/nios2_system.qip
	quartus_sh -t de10_nano_project.tcl

compile:
	quartus_sh --flow compile $(PROJECT_NAME) -c $(REVISION_NAME)

# ── Step 3: generate the Nios II HAL BSP ─────────────────────────────────────
BSP_DEFAULT_TCL = /opt/intelFPGA/nios2eds/sdk2/bin/bsp-set-defaults.tcl

bsp: $(SOPCINFO)
	mkdir -p $(BSP_DIR)
	nios2-bsp-create-settings \
	  --sopc $(SOPCINFO) \
	  --type hal \
	  --settings $(BSP_DIR)/settings.bsp \
	  --bsp-dir $(BSP_DIR) \
	  --script $(BSP_DEFAULT_TCL) \
	  --cpu-name nios2
	$(MAKE) -C $(BSP_DIR) WINDOWS_EXE=

# ── Step 4: build the C application ──────────────────────────────────────────
app: bsp
	$(MAKE) -C $(APP_DIR)

# ── Timing check ──────────────────────────────────────────────────────────────
check_timing:
	python ../../00_led_blinking/scripts/check_timing_slacks.py $(REVISION_NAME).sta.rpt 2>/dev/null || true

# ── Programming & debug targets ──────────────────────────────────────────────
usb-windows:
	@echo "Detaching USB-Blaster (busid $(USBIPD_BUSID)) from WSL2 → Windows..."
	$(USBIPD) detach --busid $(USBIPD_BUSID) 2>/dev/null || true
	@sleep 1

usb-wsl:
	@echo "Attaching USB-Blaster (busid $(USBIPD_BUSID)) to WSL2..."
	$(USBIPD) attach --wsl --busid $(USBIPD_BUSID) 2>/dev/null || true
	@sleep 1

SOF_WIN_TMP := /mnt/c/Windows/Temp
program-sof: usb-windows $(REVISION_NAME).sof
	cp $(REVISION_NAME).sof $(SOF_WIN_TMP)/$(REVISION_NAME).sof
	$(QUARTUS_PGM) -m jtag -o "p;C:\Windows\Temp\$(REVISION_NAME).sof@2"
	@$(MAKE) --no-print-directory usb-wsl

REPO_ROOT  = $(realpath ../..)
UNAME_SHIM = $(REPO_ROOT)/common/docker/uname_shim.sh
download-elf: usb-wsl $(APP_DIR)/nios2_interrupts.elf
	docker run --rm --privileged \
	  -v $(REPO_ROOT):/work \
	  -v $(UNAME_SHIM):/usr/local/bin/uname:ro \
	  $(DOCKER_IMAGE) \
	  nios2-download -g /work/06_nios2_interrupts/software/app/nios2_interrupts.elf

terminal: usb-wsl
	docker run --rm -it --privileged \
	  -v $(UNAME_SHIM):/usr/local/bin/uname:ro \
	  $(DOCKER_IMAGE) \
	  nios2-terminal

# ── Clean ─────────────────────────────────────────────────────────────────────
FILE_ENDINGS_FOR_REMOVAL = .rpt .summary .qsf .qpf .qws .done .smsg .jdi .pin .sld .sof dump.txt

clean:
	rm -rf db incremental_db output_files
	rm -rf $(QSYS_GEN_DIR) $(QSYS_FILE)
	rm -rf $(BSP_DIR)
	$(MAKE) -C $(APP_DIR) clean 2>/dev/null || true
	rm -f $(foreach ENDING,$(FILE_ENDINGS_FOR_REMOVAL),$(wildcard *$(ENDING)))

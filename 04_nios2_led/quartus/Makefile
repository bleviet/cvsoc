# Makefile for the 04_nios2_led Quartus project.
# Orchestrates: Platform Designer generation → Quartus compile → BSP generation → app build.
#
# Run inside the raetro/quartus Docker container (all tools are in PATH).
# Usage:
#   docker run --rm -v <repo_root>:/work raetro/quartus:23.1 \
#     bash -c "cd /work/04_nios2_led/quartus && make all"

PROJECT_NAME  = 04_nios2_led
REVISION_NAME = de10_nano

QSYS_TCL      = ../qsys/nios2_system.tcl
QSYS_FILE     = ../qsys/nios2_system.qsys
QSYS_GEN_DIR  = ../qsys/nios2_system_gen
SOPCINFO      = ../qsys/nios2_system.sopcinfo

BSP_DIR       = ../software/bsp
APP_DIR       = ../software/app

# Windows Quartus Programmer (callable from WSL2 via interop — no extra setup).
# Override with: make QUARTUS_PGM=<path> program-sof
QUARTUS_PGM  ?= /mnt/c/intelFPGA_lite/23.1std/qprogrammer/bin64/quartus_pgm.exe

# Docker image that contains nios2-download / nios2-terminal.
# Requires usbipd-win: https://github.com/dorssel/usbipd-win
#   1. On Windows: usbipd list   → note bus-id of "USB-Blaster"
#   2. On Windows: usbipd attach --wsl --busid <bus-id>
#   3. Then run: make download-elf  (or  make terminal)
DOCKER_IMAGE ?= raetro/quartus:23.1

.PHONY: all qsys project compile bsp app clean \
        program-sof download-elf terminal

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
# Use nios2-bsp-create-settings directly instead of the nios2-bsp wrapper,
# which misdetects WSL when running inside a Docker container on a WSL2 host.
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

# ── Timing check (reuse the script from existing projects) ───────────────────
check_timing:
	python ../../00_led_blinking/scripts/check_timing_slacks.py $(REVISION_NAME).sta.rpt 2>/dev/null || true

# ── Programming & debug targets ──────────────────────────────────────────────
# program-sof: Programs the FPGA via JTAG using the Windows Quartus Programmer.
#   Works from WSL2 without usbipd-win because quartus_pgm.exe runs on Windows.
#   The .sof is copied to C:\Windows\Temp first to avoid UNC-path issues.
SOF_WIN_TMP := /mnt/c/Windows/Temp
program-sof: $(REVISION_NAME).sof
	cp $< $(SOF_WIN_TMP)/$(REVISION_NAME).sof
	$(QUARTUS_PGM) -m jtag -o "p;C:\Windows\Temp\$(REVISION_NAME).sof@2"

# download-elf: Downloads the Nios II ELF to the soft-core via JTAG and starts it.
#   Requires the USB-Blaster attached to WSL2 first (see usbipd-win note above).
#   The uname_shim prevents nios2-download from detecting WSL inside Docker.
REPO_ROOT = $(realpath ../..)
UNAME_SHIM = $(REPO_ROOT)/common/docker/uname_shim.sh
download-elf: $(APP_DIR)/nios2_led.elf
	docker run --rm --privileged \
	  -v $(REPO_ROOT):/work \
	  -v $(UNAME_SHIM):/usr/local/bin/uname:ro \
	  $(DOCKER_IMAGE) \
	  nios2-download -g /work/04_nios2_led/software/app/nios2_led.elf

# terminal: Opens the Nios II JTAG UART terminal (Ctrl-C to quit).
#   Requires the USB-Blaster attached to WSL2 (same usbipd-win step as above).
terminal:
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

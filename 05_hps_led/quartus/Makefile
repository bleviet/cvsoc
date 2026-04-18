# Makefile for the 05_hps_led Quartus project.
# Orchestrates: Platform Designer generation → Quartus compile → ARM app build.
#
# All steps run inside the raetro/quartus:23.1 Docker container.
# Usage (from repo root):
#   docker run --rm -v $(pwd):/work raetro/quartus:23.1 \
#     bash -c "cd /work/05_hps_led/quartus && make setup && make all"
#
# The 'setup' target installs the ARM cross-compiler from the Debian snapshot
# archive.  It is only needed once per fresh container session.

PROJECT_NAME  = 05_hps_led
REVISION_NAME = de10_nano

QSYS_TCL      = ../qsys/hps_system.tcl
QSYS_FILE     = ../qsys/hps_system.qsys
QSYS_GEN_DIR  = ../qsys/hps_system/synthesis

APP_DIR       = ../software/app

ARM_CC        ?= arm-linux-gnueabihf-gcc

# Windows Quartus Programmer (callable from WSL2 via interop — no extra setup).
# Override with: make QUARTUS_PGM=<path> program-sof
QUARTUS_PGM  ?= /mnt/c/intelFPGA_lite/23.1std/qprogrammer/bin64/quartus_pgm.exe
DOCKER_IMAGE ?= raetro/quartus:23.1

# usbipd-win bus ID for the USB-Blaster II.
# Find yours with: usbipd.exe list  (look for "USB-Blaster")
# Override with: make USBIPD_BUSID=<id> program-sof
USBIPD       ?= usbipd.exe
USBIPD_BUSID ?= 2-4

# HPS board connection for deploy-elf.
# The ELF is a bare-metal app targeting HPS OCRAM (0xFFFF0000).
# Load it via JTAG (DS-5 or OpenOCD+arm-none-eabi-gdb) or copy to SD card.
HPS_IP       ?= 192.168.1.100
HPS_USER     ?= root

.PHONY: all setup qsys patch-oct project compile app clean check_timing \
        program-sof download-elf deploy-elf usb-windows usb-wsl

all: qsys project compile app

# ── Step 0: install ARM cross-compiler (Debian 9 snapshot) ───────────────
# The raetro/quartus:23.1 container is Debian 9 (stretch); the package is
# available from the snapshot archive.
setup:
	@echo "Installing ARM cross-compiler (arm-linux-gnueabihf-gcc)…"
	echo "deb http://snapshot.debian.org/archive/debian/20220622T000000Z stretch main" \
	     > /etc/apt/sources.list.d/snapshot.list
	apt-get update -o Acquire::Check-Valid-Until=false -qq
	apt-get install -y -o Acquire::Check-Valid-Until=false \
	     gcc-arm-linux-gnueabihf binutils-arm-linux-gnueabihf
	@echo "ARM cross-compiler installed: $$(arm-linux-gnueabihf-gcc --version | head -1)"

# ── Step 1: generate the .qsys file from the TCL script ──────────────────
$(QSYS_FILE): $(QSYS_TCL)
	cd ../qsys && qsys-script --script=hps_system.tcl

qsys: $(QSYS_FILE)
	qsys-generate $(QSYS_FILE) --synthesis=VERILOG
	@$(MAKE) patch-oct

# ── Patch generated DDR3 PHY to propagate USE_TERMINATION_CONTROL ────────
# qsys-generate produces altdq_dqs2_acv_connect_to_hard_phy_cyclonev.sv
# with USE_TERMINATION_CONTROL declared but never forwarded to the
# cyclonev_io_obuf primitives, causing Fitter Error 174068 when the
# SERIESTERMINATIONCONTROL port is connected. Add the missing parameter.
patch-oct:
	@echo "Patching DDR3 PHY: propagating USE_TERMINATION_CONTROL to I/O buffers…"
	@python3 ../../05_hps_led/scripts/patch_oct.py \
	    ../qsys/hps_system/synthesis/submodules/altdq_dqs2_acv_connect_to_hard_phy_cyclonev.sv

# ── Step 2: create and compile the Quartus project ────────────────────────
project: $(QSYS_GEN_DIR)/hps_system.qip
	quartus_sh -t de10_nano_project.tcl

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
	quartus_map  --read_settings_files=on --write_settings_files=off \
	             $(PROJECT_NAME) -c $(REVISION_NAME)
	quartus_sta  -t $(QSYS_GEN_DIR)/submodules/hps_sdram_p0_pin_assignments.tcl \
	             $(REVISION_NAME)
	quartus_map  --read_settings_files=on --write_settings_files=off \
	             $(PROJECT_NAME) -c $(REVISION_NAME)
	quartus_fit  --read_settings_files=on --write_settings_files=off \
	             $(PROJECT_NAME) -c $(REVISION_NAME)
	quartus_asm  --read_settings_files=on --write_settings_files=off \
	             $(PROJECT_NAME) -c $(REVISION_NAME)
	quartus_sta  $(PROJECT_NAME) -c $(REVISION_NAME)

# ── Step 3: cross-compile the ARM bare-metal application ─────────────────
app:
	$(MAKE) -C $(APP_DIR) CC=$(ARM_CC)

# ── Programming & deploy targets ──────────────────────────────────────────────
# usb-windows: Detaches the USB-Blaster from WSL2 so Windows can see it.
usb-windows:
	@echo "Detaching USB-Blaster (busid $(USBIPD_BUSID)) from WSL2 → Windows..."
	$(USBIPD) detach --busid $(USBIPD_BUSID) 2>/dev/null || true
	@sleep 1

# usb-wsl: Attaches the USB-Blaster to WSL2 so Docker can reach it.
usb-wsl:
	@echo "Attaching USB-Blaster (busid $(USBIPD_BUSID)) to WSL2..."
	$(USBIPD) attach --wsl --busid $(USBIPD_BUSID)
	@sleep 1

# program-sof: Detaches USB-Blaster from WSL2, programs the FPGA via Windows
#   quartus_pgm.exe, then re-attaches so download-elf can follow immediately.
#   The .sof is copied to C:\Windows\Temp first to avoid UNC-path issues.
SOF_WIN_TMP := /mnt/c/Windows/Temp
program-sof: usb-windows $(REVISION_NAME).sof
	cp $(REVISION_NAME).sof $(SOF_WIN_TMP)/$(REVISION_NAME).sof
	$(QUARTUS_PGM) -m jtag -o "p;C:\Windows\Temp\$(REVISION_NAME).sof@2"
	@$(MAKE) --no-print-directory usb-wsl

# download-elf: Loads the bare-metal binary into HPS OCRAM via JTAG (OpenOCD in Docker).
#   Runs OpenOCD inside the raetro/quartus container (--privileged for USB access).
OPENOCD_CFG  = ../scripts/de10_nano_hps.cfg
REPO_ROOT    = $(realpath ../..)
UNAME_SHIM   = $(REPO_ROOT)/common/docker/uname_shim.sh

download-elf: usb-wsl $(APP_DIR)/hps_led.elf
	docker run --rm --privileged \
	  -v $(REPO_ROOT):/work \
	  -v $(UNAME_SHIM):/usr/local/bin/uname:ro \
	  -v $(realpath $(OPENOCD_CFG)):/tmp/de10_nano_hps.cfg:ro \
	  $(DOCKER_IMAGE) \
	  bash -c '\
	    /opt/intelFPGA/quartus/bin/jtagd && sleep 2 && \
	    /opt/intelFPGA/quartus/bin/openocd \
	      -f /tmp/de10_nano_hps.cfg \
	      -c "init" \
	      -c "halt" \
	      -c "load_image /work/05_hps_led/software/app/hps_led.bin 0xFFFF0000 bin" \
	      -c "resume 0xFFFF0000" \
	      -c "shutdown"'
	@echo "Done: HPS LED application is running."

# deploy-elf: Copies the bare-metal HPS ELF + binary to the board over SSH.
#   Boot the board to U-Boot or Linux first, then run:
#     make HPS_IP=<board-ip> deploy-elf
#   On the board (U-Boot):  go 0xFFFF0000
deploy-elf: $(APP_DIR)/hps_led.elf
	scp $(APP_DIR)/hps_led.elf $(APP_DIR)/hps_led.bin \
	    $(HPS_USER)@$(HPS_IP):/tmp/
	@echo "ELF + binary copied to $(HPS_USER)@$(HPS_IP):/tmp/"
	@echo "  To run with U-Boot: fatload / go 0xFFFF0000"

# ── Timing check ──────────────────────────────────────────────────────────
check_timing:
	python ../../00_led_blinking/scripts/check_timing_slacks.py \
	       $(REVISION_NAME).sta.rpt 2>/dev/null || true

# ── Clean ─────────────────────────────────────────────────────────────────
FILE_ENDINGS_FOR_REMOVAL = .rpt .summary .qsf .qpf .qws .done .smsg .jdi .pin .sld .sof dump.txt

clean:
	rm -rf db incremental_db output_files hps_isw_handoff
	rm -rf $(QSYS_FILE) ../qsys/hps_system ../qsys/hps_system.sopcinfo
	$(MAKE) -C $(APP_DIR) clean 2>/dev/null || true
	rm -f $(foreach ENDING,$(FILE_ENDINGS_FOR_REMOVAL),$(wildcard *$(ENDING)))

# Makefile for the 07_hps_interrupts Quartus project.
# Orchestrates: Platform Designer generation → Quartus compile → ARM app build.
#
# All steps run inside the cvsoc/quartus:23.1 Docker container.
# Usage (from repo root):
#   docker run --rm -v $(pwd):/work cvsoc/quartus:23.1 \
#     bash -c "cd /work/07_hps_interrupts/quartus && make setup && make all"
#
# The 'setup' target installs the ARM cross-compiler from the Debian snapshot
# archive.  It is only needed once per fresh container session.

PROJECT_NAME  = 07_hps_interrupts
REVISION_NAME = de10_nano

QSYS_TCL      = ../qsys/hps_system.tcl
QSYS_FILE     = ../qsys/hps_system.qsys
QSYS_GEN_DIR  = ../qsys/hps_system/synthesis

APP_DIR       = ../software/app

ARM_CC        ?= arm-linux-gnueabihf-gcc

DOCKER_IMAGE ?= cvsoc/quartus:23.1

USBIPD       ?= usbipd.exe
USBIPD_BUSID ?= 2-4

# JTAG chain device index: 1=HPS ARM DAP, 2=Cyclone V FPGA
DEVICE_INDEX ?= 2

REPO_ROOT    = $(realpath ../..)
UNAME_SHIM   = $(REPO_ROOT)/common/docker/uname_shim.sh

HPS_IP       ?= 192.168.1.100
HPS_USER     ?= root

.PHONY: all setup qsys patch-oct project compile app clean check_timing \
        program-sof download-elf deploy-elf usb-windows usb-wsl

all: qsys project compile app

# ── Step 0: ARM cross-compiler (pre-installed in cvsoc/quartus:23.1) ─────
# The ARM cross-compiler is baked into cvsoc/quartus:23.1 — no manual install
# needed.  This target is kept for informational purposes.
setup:
	@echo "ARM cross-compiler status:"
	@arm-linux-gnueabihf-gcc --version | head -1
	@arm-none-eabi-gdb --version | head -1
	@echo "All ARM tools are pre-installed in cvsoc/quartus:23.1."

# ── Step 1: generate the .qsys file from the TCL script ──────────────────
$(QSYS_FILE): $(QSYS_TCL)
	cd ../qsys && qsys-script --script=hps_system.tcl

qsys: $(QSYS_FILE)
	qsys-generate $(QSYS_FILE) --synthesis=VERILOG
	@$(MAKE) patch-oct

# ── Patch generated DDR3 PHY (same fix as 05_hps_led) ────────────────────
patch-oct:
	@echo "Patching DDR3 PHY: propagating USE_TERMINATION_CONTROL to I/O buffers…"
	@python3 ../scripts/patch_oct.py \
	    ../qsys/hps_system/synthesis/submodules/altdq_dqs2_acv_connect_to_hard_phy_cyclonev.sv

# ── Step 2: create and compile the Quartus project ────────────────────────
project: $(QSYS_GEN_DIR)/hps_system.qip
	quartus_sh -t de10_nano_project.tcl

# Two-pass compilation flow required for Cyclone V HPS DDR3 (same as 05_hps_led).
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
program-sof: usb-wsl $(REVISION_NAME).sof
	docker run --rm --privileged \
	  -v /dev/bus/usb:/dev/bus/usb \
	  -v $(REPO_ROOT):/work \
	  $(DOCKER_IMAGE) \
	  bash -c 'jtagd && sleep 2 && \
	    quartus_pgm -m jtag -o "p;/work/$(PROJECT_NAME)/quartus/$(REVISION_NAME).sof@$(DEVICE_INDEX)"'

OPENOCD_CFG  = ../scripts/de10_nano_hps.cfg

download-elf: usb-wsl $(APP_DIR)/hps_interrupts.elf
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
	      -c "load_image /work/07_hps_interrupts/software/app/hps_interrupts.bin 0xFFFF0000 bin" \
	      -c "resume 0xFFFF0000" \
	      -c "shutdown"'
	@echo "Done: HPS interrupt application is running."

deploy-elf: $(APP_DIR)/hps_interrupts.elf
	scp $(APP_DIR)/hps_interrupts.elf $(APP_DIR)/hps_interrupts.bin \
	    $(HPS_USER)@$(HPS_IP):/tmp/
	@echo "ELF + binary copied to $(HPS_USER)@$(HPS_IP):/tmp/"

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

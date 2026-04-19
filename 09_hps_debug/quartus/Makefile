# Makefile for the 09_hps_debug Quartus project.
# Orchestrates: Platform Designer generation → Quartus compile → ARM app build.
# Adds OpenOCD server and interactive GDB debug targets on top of the 07_hps_interrupts flow.
#
# All steps run inside the cvsoc/quartus:23.1 Docker container.
# Usage (from repo root):
#   docker run --rm -v $(pwd):/work cvsoc/quartus:23.1 \
#     bash -c "cd /work/09_hps_debug/quartus && make setup && make all"

PROJECT_NAME  = 09_hps_debug
REVISION_NAME = de10_nano

QSYS_TCL      = ../qsys/hps_system.tcl
QSYS_FILE     = ../qsys/hps_system.qsys
QSYS_GEN_DIR  = ../qsys/hps_system/synthesis

APP_DIR       = ../software/app

ARM_CC        ?= arm-linux-gnueabihf-gcc

QUARTUS_PGM  ?= /mnt/c/intelFPGA_lite/23.1std/qprogrammer/bin64/quartus_pgm.exe
DOCKER_IMAGE ?= cvsoc/quartus:23.1

USBIPD       ?= usbipd.exe
USBIPD_BUSID ?= 2-4

HPS_IP       ?= 192.168.1.100
HPS_USER     ?= root

# GDB port for OpenOCD
GDB_PORT     ?= 3333

.PHONY: all setup qsys patch-oct project compile app clean check_timing \
        program-sof download-elf deploy-elf \
        openocd gdb \
        usb-windows usb-wsl

all: qsys project compile app

# ── Step 0: ARM cross-compiler (pre-installed in cvsoc/quartus:23.1) ─────
# The ARM cross-compiler and GDB are baked into cvsoc/quartus:23.1 — no
# manual install needed.  This target is kept for informational purposes.
setup:
	@echo "ARM toolchain status:"
	@arm-linux-gnueabihf-gcc --version | head -1
	@arm-none-eabi-gdb --version | head -1
	@echo "All ARM tools are pre-installed in cvsoc/quartus:23.1."

# ── Step 1: generate the .qsys file from the TCL script ──────────────────────
$(QSYS_FILE): $(QSYS_TCL)
	cd ../qsys && qsys-script --script=hps_system.tcl

qsys: $(QSYS_FILE)
	qsys-generate $(QSYS_FILE) --synthesis=VERILOG
	@$(MAKE) patch-oct

# ── Patch generated DDR3 PHY (same fix as 05_hps_led) ────────────────────────
patch-oct:
	@echo "Patching DDR3 PHY: propagating USE_TERMINATION_CONTROL to I/O buffers…"
	@python3 ../scripts/patch_oct.py \
	    ../qsys/hps_system/synthesis/submodules/altdq_dqs2_acv_connect_to_hard_phy_cyclonev.sv

# ── Step 2: create and compile the Quartus project ────────────────────────────
project: $(QSYS_GEN_DIR)/hps_system.qip
	quartus_sh -t de10_nano_project.tcl

# Two-pass compilation flow required for Cyclone V HPS DDR3 (same as 07_hps_interrupts).
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

# ── Step 3: cross-compile the ARM bare-metal application ─────────────────────
app:
	$(MAKE) -C $(APP_DIR) CC=$(ARM_CC)

# ── Timing check ──────────────────────────────────────────────────────────────
check_timing:
	python ../../00_led_blinking/scripts/check_timing_slacks.py \
	       $(REVISION_NAME).sta.rpt 2>/dev/null || true

# ── USB-Blaster attach/detach ─────────────────────────────────────────────────
usb-windows:
	@echo "Detaching USB-Blaster (busid $(USBIPD_BUSID)) from WSL2 → Windows..."
	$(USBIPD) detach --busid $(USBIPD_BUSID) 2>/dev/null || true
	@sleep 1

usb-wsl:
	@echo "Attaching USB-Blaster (busid $(USBIPD_BUSID)) to WSL2..."
	$(USBIPD) attach --wsl --busid $(USBIPD_BUSID) 2>/dev/null || true
	@sleep 1

# ── Program FPGA ──────────────────────────────────────────────────────────────
SOF_WIN_TMP := /mnt/c/Windows/Temp
program-sof: usb-windows $(REVISION_NAME).sof
	cp $(REVISION_NAME).sof $(SOF_WIN_TMP)/$(REVISION_NAME).sof
	$(QUARTUS_PGM) -m jtag -o "p;C:\Windows\Temp\$(REVISION_NAME).sof@2"
	@$(MAKE) --no-print-directory usb-wsl

# ── Download ELF (load and run, no interactive debugging) ────────────────────
OPENOCD_CFG  = ../scripts/de10_nano_hps.cfg
REPO_ROOT    = $(realpath ../..)
UNAME_SHIM   = $(REPO_ROOT)/common/docker/uname_shim.sh

download-elf: usb-wsl $(APP_DIR)/hps_debug.elf
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
	      -c "load_image /work/09_hps_debug/software/app/hps_debug.bin 0xFFFF0000 bin" \
	      -c "resume 0xFFFF0000" \
	      -c "shutdown"'
	@echo "Done: HPS debug application is running."

deploy-elf: $(APP_DIR)/hps_debug.elf
	scp $(APP_DIR)/hps_debug.elf $(APP_DIR)/hps_debug.bin \
	    $(HPS_USER)@$(HPS_IP):/tmp/
	@echo "ELF + binary copied to $(HPS_USER)@$(HPS_IP):/tmp/"

# ── OpenOCD server (start OpenOCD as GDB server, keep running) ───────────────
#
# Run this in one terminal, then run 'make gdb' in a second terminal.
# OpenOCD resets the Cortex-A9, waits for a GDB connection on port $(GDB_PORT).
#
# NOTE: Exposes port $(GDB_PORT) from the Docker container to localhost so
#       the second 'make gdb' container can connect via --network host.
openocd: usb-wsl
	docker run --rm -it --privileged \
	  --name hps-openocd \
	  -p $(GDB_PORT):$(GDB_PORT) \
	  -p 4444:4444 \
	  -v $(REPO_ROOT):/work \
	  -v $(UNAME_SHIM):/usr/local/bin/uname:ro \
	  -v $(realpath $(OPENOCD_CFG)):/tmp/de10_nano_hps.cfg:ro \
	  $(DOCKER_IMAGE) \
	  bash -c '\
	    /opt/intelFPGA/quartus/bin/jtagd && sleep 2 && \
	    /opt/intelFPGA/quartus/bin/openocd \
	      -f /tmp/de10_nano_hps.cfg \
	      -c "init" \
	      -c "halt"'

# ── Interactive GDB session ───────────────────────────────────────────────────
#
# Connects arm-linux-gnueabihf-gdb to the running OpenOCD server.
# The GDB init script connects to OpenOCD, sets breakpoints, and continues.
#
# Usage:
#   Terminal 1: make openocd
#   Terminal 2: make gdb
gdb: $(APP_DIR)/hps_debug.elf
	docker run --rm -it \
	  --network host \
	  -v $(REPO_ROOT):/work \
	  $(DOCKER_IMAGE) \
	  arm-none-eabi-gdb \
	    -ex "set pagination off" \
	    -x /work/09_hps_debug/scripts/hps_debug.gdb \
	    /work/09_hps_debug/software/app/hps_debug.elf

# ── Clean ─────────────────────────────────────────────────────────────────────
FILE_ENDINGS_FOR_REMOVAL = .rpt .summary .qsf .qpf .qws .done .smsg .jdi .pin .sld .sof dump.txt

clean:
	rm -rf db incremental_db output_files hps_isw_handoff
	rm -rf $(QSYS_FILE) ../qsys/hps_system ../qsys/hps_system.sopcinfo
	$(MAKE) -C $(APP_DIR) clean 2>/dev/null || true
	rm -f $(foreach ENDING,$(FILE_ENDINGS_FOR_REMOVAL),$(wildcard *$(ENDING)))

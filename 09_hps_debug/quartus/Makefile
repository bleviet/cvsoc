# Makefile for the 09_hps_debug Quartus project.
# Orchestrates: Platform Designer generation → Quartus compile → ARM app build.
# Adds OpenOCD server and interactive GDB debug targets on top of the 07_hps_interrupts flow.
#
# All steps run inside the cvsoc/quartus:23.1 Docker container.
# Call any target directly from the WSL2 host — Docker is invoked automatically.

PROJECT_NAME  = 09_hps_debug
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
DOCKER       = docker run --rm -v $(REPO_ROOT):/work $(DOCKER_IMAGE) bash -c
DOCKER_CWD   = cd /work/$(PROJECT_NAME)/quartus
# GUI helper: adds X11/WSLg display forwarding for Quartus GUI targets.
DOCKER_GUI   = docker run --rm \
  -e DISPLAY=$$DISPLAY \
  -v /tmp/.X11-unix:/tmp/.X11-unix \
  -v /mnt/wslg:/mnt/wslg \
  -e WAYLAND_DISPLAY=$$WAYLAND_DISPLAY \
  -e XDG_RUNTIME_DIR=/mnt/wslg/runtime-dir \
  -v $(REPO_ROOT):/work \
  $(DOCKER_IMAGE) bash -c

HPS_IP       ?= 192.168.1.100
HPS_USER     ?= root

# GDB port for OpenOCD
GDB_PORT     ?= 3333

.PHONY: all setup qsys patch-oct project compile app clean check_timing \
        program-sof download-elf deploy-elf \
        openocd gdb \
        usb-windows usb-wsl \
        quartus-gui platform-designer

all:
	docker run --rm -v $(REPO_ROOT):/work $(DOCKER_IMAGE) bash -c '\
	  cd /work/$(PROJECT_NAME)/qsys && qsys-script --script=hps_system.tcl && \
	  cd /work/$(PROJECT_NAME)/quartus && \
	  qsys-generate ../qsys/hps_system.qsys --synthesis=VERILOG && \
	  python3 ../scripts/patch_oct.py \
	    ../qsys/hps_system/synthesis/submodules/altdq_dqs2_acv_connect_to_hard_phy_cyclonev.sv && \
	  quartus_sh -t de10_nano_project.tcl && \
	  quartus_map --read_settings_files=on --write_settings_files=off $(PROJECT_NAME) -c $(REVISION_NAME) && \
	  quartus_sta -t ../qsys/hps_system/synthesis/submodules/hps_sdram_p0_pin_assignments.tcl $(REVISION_NAME) && \
	  quartus_map --read_settings_files=on --write_settings_files=off $(PROJECT_NAME) -c $(REVISION_NAME) && \
	  quartus_fit --read_settings_files=on --write_settings_files=off $(PROJECT_NAME) -c $(REVISION_NAME) && \
	  quartus_asm --read_settings_files=on --write_settings_files=off $(PROJECT_NAME) -c $(REVISION_NAME) && \
	  quartus_sta $(PROJECT_NAME) -c $(REVISION_NAME) && \
	  make -C ../software/app CC=arm-linux-gnueabihf-gcc'

# ── Step 0: ARM cross-compiler (pre-installed in cvsoc/quartus:23.1) ─────
# The ARM cross-compiler and GDB are baked into cvsoc/quartus:23.1 — no
# manual install needed.  This target is kept for informational purposes.
setup:
	$(DOCKER) "echo 'ARM toolchain status:' && \
	  arm-linux-gnueabihf-gcc --version | head -1 && \
	  arm-none-eabi-gdb --version | head -1 && \
	  echo 'All ARM tools are pre-installed in cvsoc/quartus:23.1.'"

# ── Step 1: generate the .qsys file from the TCL script ──────────────────────
$(QSYS_FILE): $(QSYS_TCL)
	$(DOCKER) "$(DOCKER_CWD) && cd ../qsys && qsys-script --script=hps_system.tcl"

qsys: $(QSYS_FILE)
	$(DOCKER) "$(DOCKER_CWD) && \
	  qsys-generate $(QSYS_FILE) --synthesis=VERILOG && \
	  python3 ../scripts/patch_oct.py \
	    ../qsys/hps_system/synthesis/submodules/altdq_dqs2_acv_connect_to_hard_phy_cyclonev.sv"

# ── Patch generated DDR3 PHY (same fix as 05_hps_led) ────────────────────────
patch-oct:
	$(DOCKER) "$(DOCKER_CWD) && \
	  python3 ../scripts/patch_oct.py \
	    ../qsys/hps_system/synthesis/submodules/altdq_dqs2_acv_connect_to_hard_phy_cyclonev.sv"

# ── Step 2: create and compile the Quartus project ────────────────────────────
project: $(QSYS_GEN_DIR)/hps_system.qip
	$(DOCKER) "$(DOCKER_CWD) && quartus_sh -t de10_nano_project.tcl"

# Two-pass compilation flow required for Cyclone V HPS DDR3 (same as 07_hps_interrupts).
compile:
	$(DOCKER) "$(DOCKER_CWD) && \
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
	  quartus_sta $(PROJECT_NAME) -c $(REVISION_NAME)"

# ── Step 3: cross-compile the ARM bare-metal application ─────────────────────
app:
	$(DOCKER) "$(DOCKER_CWD) && make -C $(APP_DIR) CC=$(ARM_CC)"

# ── Timing check ──────────────────────────────────────────────────────────────
check_timing:
	$(DOCKER) "$(DOCKER_CWD) && python /work/00_led_blinking/scripts/check_timing_slacks.py \
	       $(REVISION_NAME).sta.rpt 2>/dev/null || true"

# ── USB-Blaster attach/detach ─────────────────────────────────────────────────
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

# ── Program FPGA ──────────────────────────────────────────────────────────────
# program-sof: Programs the FPGA bitstream via JTAG entirely from WSL2.
#   Starts jtagd inside the container then calls quartus_pgm.
program-sof: usb-wsl $(REVISION_NAME).sof
	docker run --rm --privileged \
	  -v /dev/bus/usb:/dev/bus/usb \
	  -v $(REPO_ROOT):/work \
	  $(DOCKER_IMAGE) \
	  bash -c 'jtagd && sleep 2 && \
	    quartus_pgm -m jtag -o "p;/work/$(PROJECT_NAME)/quartus/$(REVISION_NAME).sof@$(DEVICE_INDEX)"; \
	    kill $$(pgrep jtagd) 2>/dev/null || true'

# ── Download ELF (load and run, no interactive debugging) ────────────────────
OPENOCD_CFG  = ../scripts/de10_nano_hps.cfg

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
	      -c "shutdown"; \
	    kill $$(pgrep jtagd) 2>/dev/null || true'
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
	      -c "halt"; \
	    kill $$(pgrep jtagd) 2>/dev/null || true'

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

# ── GUI targets ───────────────────────────────────────────────────────────────
# Opens a GUI tool inside Docker with X11 forwarding to the WSL2 host.
# Requires WSLg (Windows 11) or a running X server (DISPLAY must be set).

# quartus-gui: Opens Quartus Prime with the project loaded.
#   Run 'make project' first if de10_nano.qpf does not yet exist.
quartus-gui:
	$(DOCKER_GUI) "$(DOCKER_CWD) && quartus $(PROJECT_NAME).qpf 2>/dev/null || quartus"

# platform-designer: Opens Platform Designer with the .qsys system file.
#   Run 'make $(QSYS_FILE)' first if the .qsys file does not yet exist.
platform-designer:
	$(DOCKER_GUI) "$(DOCKER_CWD) && qsys-edit $(QSYS_FILE) 2>/dev/null || qsys-edit"

# ── Clean ─────────────────────────────────────────────────────────────────────
FILE_ENDINGS_FOR_REMOVAL = .rpt .summary .qsf .qpf .qws .done .smsg .jdi .pin .sld .sof dump.txt

clean:
	$(DOCKER) "$(DOCKER_CWD) && \
	  rm -rf db incremental_db output_files hps_isw_handoff && \
	  rm -rf $(QSYS_FILE) ../qsys/hps_system ../qsys/hps_system.sopcinfo && \
	  make -C $(APP_DIR) clean 2>/dev/null || true && \
	  find . -maxdepth 1 \( -name '*.rpt' -o -name '*.summary' -o -name '*.qsf' \
	    -o -name '*.qpf' -o -name '*.qws' -o -name '*.done' -o -name '*.smsg' \
	    -o -name '*.jdi' -o -name '*.pin' -o -name '*.sld' -o -name '*.sof' \
	    -o -name 'dump.txt' \) -delete"

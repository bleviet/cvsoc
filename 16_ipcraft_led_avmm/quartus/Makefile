# Makefile for the 16_ipcraft_led_avmm Quartus project.
# Orchestrates: Platform Designer generation → Quartus compile → BSP generation → app build.
#
# All steps run inside the cvsoc/quartus:23.1 Docker container.
# Call any target directly from the WSL2 host — Docker is invoked automatically.
#
# led_controller_avmm's RTL/altera/tb are IPCraft-generated (../led_controller_avmm.ip.yml,
# .mm.yml); this Makefile only builds the board-level project around it, following
# 04_nios2_led's exact convention.

PROJECT_NAME  = 16_ipcraft_led_avmm
REVISION_NAME = de10_nano

QSYS_TCL      = ../qsys/led_avmm_system.tcl
QSYS_FILE     = ../qsys/led_avmm_system.qsys
QSYS_GEN_DIR  = ../qsys/led_avmm_system_gen
SOPCINFO      = ../qsys/led_avmm_system.sopcinfo

# Debug variant: uses led_avmm_system_debug.tcl which adds a JTAG-to-Avalon-MM
# master for System Console register access (issue #36 Part D fabric plumbing).
DEBUG_QSYS_TCL = ../qsys/led_avmm_system_debug.tcl
DEBUG_DIR      = ../debug
LED_CTRL_BASE  = 0x00010010

BSP_DIR       = ../software/bsp
APP_DIR       = ../software/app

DOCKER_IMAGE ?= cvsoc/quartus:23.1

# usbipd-win bus ID for the USB-Blaster II.
# Find yours with: usbipd.exe list  (look for "USB-Blaster")
# Override with: make USBIPD_BUSID=<id> program-sof
USBIPD       ?= usbipd.exe
USBIPD_BUSID ?= 8-3

# JTAG chain device index: 1=HPS ARM DAP, 2=Cyclone V FPGA
DEVICE_INDEX ?= 2

REPO_ROOT  = $(realpath ../..)
UNAME_SHIM = $(REPO_ROOT)/common/docker/uname_shim.sh
DOCKER     = docker run --rm --user $$(id -u):$$(id -g) -v $(REPO_ROOT):/work $(DOCKER_IMAGE) bash -c
DOCKER_CWD = cd /work/$(PROJECT_NAME)/quartus
# GUI helper: adds X11/WSLg display forwarding for Quartus GUI targets.
DOCKER_GUI = docker run --rm --user $$(id -u):$$(id -g) \
  -e DISPLAY=$$DISPLAY \
  -v /tmp/.X11-unix:/tmp/.X11-unix \
  -v /mnt/wslg:/mnt/wslg \
  -e WAYLAND_DISPLAY=$$WAYLAND_DISPLAY \
  -e XDG_RUNTIME_DIR=/mnt/wslg/runtime-dir \
  -v $(REPO_ROOT):/work \
  $(DOCKER_IMAGE) bash -c

.PHONY: all qsys project compile bsp app clean check_timing \
        program-sof download-elf terminal usb-windows usb-wsl \
        quartus-gui platform-designer \
        debug-qsys debug-build debug-program debug-read-all debug-write-led \
        debug-dump debug-poll debug-console

all:
	docker run --rm --user $$(id -u):$$(id -g) -v $(REPO_ROOT):/work $(DOCKER_IMAGE) bash -c '\
	  cd /work/$(PROJECT_NAME)/qsys && qsys-script --script=led_avmm_system.tcl && \
	  cd /work/$(PROJECT_NAME)/quartus && \
	  qsys-generate ../qsys/led_avmm_system.qsys --synthesis=VHDL --output-directory=../qsys/led_avmm_system_gen \
	    --search-path=/work/$(PROJECT_NAME)/qsys,$$ && \
	  quartus_sh -t de10_nano_project.tcl && \
	  quartus_sh --flow compile $(PROJECT_NAME) -c $(REVISION_NAME) && \
	  mkdir -p ../software/bsp && \
	  nios2-bsp-create-settings --sopc ../qsys/led_avmm_system.sopcinfo --type hal \
	    --settings ../software/bsp/settings.bsp --bsp-dir ../software/bsp \
	    --script /opt/intelFPGA/nios2eds/sdk2/bin/bsp-set-defaults.tcl --cpu-name nios2 && \
	  make -C ../software/bsp WINDOWS_EXE= && \
	  make -C ../software/app'

# ── Step 1: generate the .qsys file from the TCL script ──────────────────────
# led_controller_avmm_hw.tcl is symlinked into ../qsys/ (-> ../altera/led_controller_avmm_hw.tcl)
# so qsys-script finds the custom component in its own cwd, same as
# 15_ddr3_fpga_hps co-locates ddr3_test_master_hw.tcl next to hps_system.tcl.
$(QSYS_FILE): $(QSYS_TCL)
	$(DOCKER) "$(DOCKER_CWD) && cd ../qsys && qsys-script --script=led_avmm_system.tcl"

qsys: $(QSYS_FILE)
	$(DOCKER) "$(DOCKER_CWD) && qsys-generate $(QSYS_FILE) --synthesis=VHDL --output-directory=$(QSYS_GEN_DIR) \
	  --search-path=/work/$(PROJECT_NAME)/qsys,\$$"

# ── Step 2: create and compile the Quartus project ───────────────────────────
project: $(QSYS_GEN_DIR)/synthesis/led_avmm_system.qip
	$(DOCKER) "$(DOCKER_CWD) && quartus_sh -t de10_nano_project.tcl"

compile:
	$(DOCKER) "$(DOCKER_CWD) && quartus_sh --flow compile $(PROJECT_NAME) -c $(REVISION_NAME)"

# ── Step 3: generate the Nios II HAL BSP ─────────────────────────────────────
# Use nios2-bsp-create-settings directly instead of the nios2-bsp wrapper,
# which misdetects WSL when running inside a Docker container on a WSL2 host.
BSP_DEFAULT_TCL = /opt/intelFPGA/nios2eds/sdk2/bin/bsp-set-defaults.tcl

bsp: $(SOPCINFO)
	$(DOCKER) "$(DOCKER_CWD) && \
	  mkdir -p $(BSP_DIR) && \
	  nios2-bsp-create-settings \
	    --sopc $(SOPCINFO) \
	    --type hal \
	    --settings $(BSP_DIR)/settings.bsp \
	    --bsp-dir $(BSP_DIR) \
	    --script $(BSP_DEFAULT_TCL) \
	    --cpu-name nios2 && \
	  make -C $(BSP_DIR) WINDOWS_EXE="

# ── Step 4: build the C application ──────────────────────────────────────────
app: bsp
	$(DOCKER) "$(DOCKER_CWD) && make -C $(APP_DIR)"

# ── Timing check (reuse the script from existing projects) ───────────────────
check_timing:
	$(DOCKER) "$(DOCKER_CWD) && python /work/00_led_blinking/scripts/check_timing_slacks.py $(REVISION_NAME).sta.rpt 2>/dev/null || true"

# ── Programming & debug targets ──────────────────────────────────────────────
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
	docker run --rm --user $$(id -u):$$(id -g) --privileged \
	  -v /dev/bus/usb:/dev/bus/usb \
	  -v $(REPO_ROOT):/work \
	  $(DOCKER_IMAGE) \
	  bash -c 'jtagd && sleep 2 && \
	    quartus_pgm -m jtag -o "p;/work/$(PROJECT_NAME)/quartus/$(REVISION_NAME).sof@$(DEVICE_INDEX)"; \
	    kill $$(pgrep jtagd) 2>/dev/null || true'

# download-elf: Downloads the Nios II ELF to the soft-core via JTAG and starts it.
#   The uname_shim prevents nios2-download from detecting WSL inside Docker.
download-elf: usb-wsl $(APP_DIR)/led_avmm_demo.elf
	docker run --rm --user $$(id -u):$$(id -g) --privileged \
	  -v $(REPO_ROOT):/work \
	  -v $(UNAME_SHIM):/usr/local/bin/uname:ro \
	  $(DOCKER_IMAGE) \
	  nios2-download -g /work/$(PROJECT_NAME)/software/app/led_avmm_demo.elf

# terminal: Opens the Nios II JTAG UART terminal (Ctrl-C to quit).
terminal: usb-wsl
	docker run --rm --user $$(id -u):$$(id -g) -it --privileged \
	  -v $(UNAME_SHIM):/usr/local/bin/uname:ro \
	  $(DOCKER_IMAGE) \
	  nios2-terminal

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

# ── Debug variant: JTAG-to-Avalon-MM master + System Console targets ──────────
# These targets build and exercise a debug variant of the board design that
# includes an altera_jtag_avalon_master connected to led_ctrl.S_AVMM, enabling
# register peek/poke via System Console without Nios II firmware.
#
# This is the hardware validation for ipcraft-vscode issue #36 Part B/D:
# the SystemConsoleTransport (TypeScript) and debug_console.py (Python) both
# talk to the JTAG master exposed by this bitstream.
#
# Quick start:
#   make debug-build         # build the debug bitstream
#   make debug-program       # program it to the FPGA
#   make debug-read-all      # read all registers via System Console
#   make debug-write-led VALUE=0xFF  # write LED_PATTERN and verify
#   make debug-dump          # Python debug_console.py dump (all registers + fields)
#   make debug-poll REG=EVENTS COUNT=10 INTERVAL=0.5  # poll a register

# debug-qsys: Generate the .qsys from the debug variant TCL (adds JTAG master)
debug-qsys:
	$(DOCKER) "$(DOCKER_CWD) && cd ../qsys && qsys-script --script=led_avmm_system_debug.tcl"
	$(DOCKER) "$(DOCKER_CWD) && qsys-generate $(QSYS_FILE) --synthesis=VHDL --output-directory=$(QSYS_GEN_DIR) \
	  --search-path=/work/$(PROJECT_NAME)/qsys,\$$"

# debug-build: Full build of the debug variant (qsys + project + compile)
debug-build: debug-qsys
	$(DOCKER) "$(DOCKER_CWD) && quartus_sh -t de10_nano_project.tcl && \
	  quartus_sh --flow compile $(PROJECT_NAME) -c $(REVISION_NAME)"

# debug-program: Program the debug variant bitstream to the FPGA
debug-program: usb-wsl $(REVISION_NAME).sof
	docker run --rm --user $$(id -u):$$(id -g) --privileged \
	  -v /dev/bus/usb:/dev/bus/usb \
	  -v $(REPO_ROOT):/work \
	  $(DOCKER_IMAGE) \
	  bash -c 'jtagd && sleep 5 && \
	    quartus_pgm -m jtag -o "p;/work/$(PROJECT_NAME)/quartus/$(REVISION_NAME).sof@$(DEVICE_INDEX)"; \
	    kill $$(pgrep jtagd) 2>/dev/null || true'

# debug-read-all: Read all led_controller_avmm registers via System Console Tcl
debug-read-all: usb-wsl
	docker run --rm --user $$(id -u):$$(id -g) --privileged \
	  -v /dev/bus/usb:/dev/bus/usb \
	  -v $(REPO_ROOT):/work \
	  $(DOCKER_IMAGE) \
	  bash -c 'jtagd && sleep 5 && \
	    printf "source /work/$(PROJECT_NAME)/debug/read_all_registers.tcl\n" | system-console --cli; \
	    kill $$(pgrep jtagd) 2>/dev/null || true'

# debug-write-led: Write LED_PATTERN register and verify readback
#   Usage: make debug-write-led VALUE=0xFF
debug-write-led: usb-wsl
	$(eval VAL := $(or $(VALUE),0xFF))
	docker run --rm --user $$(id -u):$$(id -g) --privileged \
	  -v /dev/bus/usb:/dev/bus/usb \
	  -v $(REPO_ROOT):/work \
	  $(DOCKER_IMAGE) \
	  bash -c 'jtagd && sleep 5 && \
	    printf "source /work/$(PROJECT_NAME)/debug/write_led_pattern.tcl\n" | system-console --cli; \
	    kill $$(pgrep jtagd) 2>/dev/null || true'

# debug-dump: Python debug_console.py dump (all registers + field decode)
debug-dump: usb-wsl
	docker run --rm --user $$(id -u):$$(id -g) --privileged \
	  -v /dev/bus/usb:/dev/bus/usb \
	  -v $(REPO_ROOT):/work \
	  -v /tmp:/tmp \
	  $(DOCKER_IMAGE) \
	  bash -c 'jtagd && sleep 5 && \
	    python3 /work/$(PROJECT_NAME)/debug/debug_console.py \
	      --base $(LED_CTRL_BASE) --timeout 60 dump; \
	    kill $$(pgrep jtagd) 2>/dev/null || true'

# debug-poll: Poll a register repeatedly (Watch feature prototype)
#   Usage: make debug-poll REG=EVENTS COUNT=10 INTERVAL=0.5
debug-poll: usb-wsl
	$(eval REG := $(or $(REG),EVENTS))
	$(eval COUNT := $(or $(COUNT),10))
	$(eval INTERVAL := $(or $(INTERVAL),0.5))
	docker run --rm --user $$(id -u):$$(id -g) --privileged \
	  -v /dev/bus/usb:/dev/bus/usb \
	  -v $(REPO_ROOT):/work \
	  -v /tmp:/tmp \
	  $(DOCKER_IMAGE) \
	  bash -c 'jtagd && sleep 5 && \
	    python3 /work/$(PROJECT_NAME)/debug/debug_console.py \
	      --base $(LED_CTRL_BASE) --timeout 60 poll $(REG) $(COUNT) $(INTERVAL); \
	    kill $$(pgrep jtagd) 2>/dev/null || true'

# debug-console: Interactive Python debug console (for manual exploration)
debug-console: usb-wsl
	docker run --rm --user $$(id -u):$$(id -g) -it --privileged \
	  -v /dev/bus/usb:/dev/bus/usb \
	  -v $(REPO_ROOT):/work \
	  -v /tmp:/tmp \
	  $(DOCKER_IMAGE) \
	  bash -c 'jtagd && sleep 5 && \
	    python3 /work/$(PROJECT_NAME)/debug/debug_console.py \
	      --base $(LED_CTRL_BASE) --timeout 60 dump; \
	    kill $$(pgrep jtagd) 2>/dev/null || true'

# ── Clean ─────────────────────────────────────────────────────────────────────
FILE_ENDINGS_FOR_REMOVAL = .rpt .summary .qsf .qpf .qws .done .smsg .jdi .pin .sld .sof dump.txt

clean:
	$(DOCKER) "$(DOCKER_CWD) && \
	  rm -rf db incremental_db output_files && \
	  rm -rf $(QSYS_GEN_DIR) $(QSYS_FILE) && \
	  rm -rf $(BSP_DIR) && \
	  make -C $(APP_DIR) clean 2>/dev/null || true && \
	  find . -maxdepth 1 \( -name '*.rpt' -o -name '*.summary' -o -name '*.qsf' \
	    -o -name '*.qpf' -o -name '*.qws' -o -name '*.done' -o -name '*.smsg' \
	    -o -name '*.jdi' -o -name '*.pin' -o -name '*.sld' -o -name '*.sof' \
	    -o -name 'dump.txt' \) -delete"

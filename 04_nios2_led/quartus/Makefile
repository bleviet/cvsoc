# Makefile for the 04_nios2_led Quartus project.
# Orchestrates: Platform Designer generation → Quartus compile → BSP generation → app build.
#
# All steps run inside the cvsoc/quartus:23.1 Docker container.
# Call any target directly from the WSL2 host — Docker is invoked automatically.

PROJECT_NAME  = 04_nios2_led
REVISION_NAME = de10_nano

QSYS_TCL      = ../qsys/nios2_system.tcl
QSYS_FILE     = ../qsys/nios2_system.qsys
QSYS_GEN_DIR  = ../qsys/nios2_system_gen
SOPCINFO      = ../qsys/nios2_system.sopcinfo

BSP_DIR       = ../software/bsp
APP_DIR       = ../software/app

DOCKER_IMAGE ?= cvsoc/quartus:23.1

# usbipd-win bus ID for the USB-Blaster II.
# Find yours with: usbipd.exe list  (look for "USB-Blaster")
# Override with: make USBIPD_BUSID=<id> program-sof
USBIPD       ?= usbipd.exe
USBIPD_BUSID ?= 2-4

# JTAG chain device index: 1=HPS ARM DAP, 2=Cyclone V FPGA
DEVICE_INDEX ?= 2

REPO_ROOT  = $(realpath ../..)
UNAME_SHIM = $(REPO_ROOT)/common/docker/uname_shim.sh
DOCKER     = docker run --rm -v $(REPO_ROOT):/work $(DOCKER_IMAGE) bash -c
DOCKER_CWD = cd /work/$(PROJECT_NAME)/quartus
# GUI helper: adds X11/WSLg display forwarding for Quartus GUI targets.
DOCKER_GUI = docker run --rm \
  -e DISPLAY=$$DISPLAY \
  -v /tmp/.X11-unix:/tmp/.X11-unix \
  -v /mnt/wslg:/mnt/wslg \
  -e WAYLAND_DISPLAY=$$WAYLAND_DISPLAY \
  -e XDG_RUNTIME_DIR=/mnt/wslg/runtime-dir \
  -v $(REPO_ROOT):/work \
  $(DOCKER_IMAGE) bash -c

.PHONY: all qsys project compile bsp app clean check_timing \
        program-sof download-elf terminal usb-windows usb-wsl \
        quartus-gui platform-designer

all:
	docker run --rm -v $(REPO_ROOT):/work $(DOCKER_IMAGE) bash -c '\
	  cd /work/$(PROJECT_NAME)/qsys && qsys-script --script=nios2_system.tcl && \
	  cd /work/$(PROJECT_NAME)/quartus && \
	  qsys-generate ../qsys/nios2_system.qsys --synthesis=VHDL --output-directory=../qsys/nios2_system_gen && \
	  quartus_sh -t de10_nano_project.tcl && \
	  quartus_sh --flow compile $(PROJECT_NAME) -c $(REVISION_NAME) && \
	  mkdir -p ../software/bsp && \
	  nios2-bsp-create-settings --sopc ../qsys/nios2_system.sopcinfo --type hal \
	    --settings ../software/bsp/settings.bsp --bsp-dir ../software/bsp \
	    --script /opt/intelFPGA/nios2eds/sdk2/bin/bsp-set-defaults.tcl --cpu-name nios2 && \
	  make -C ../software/bsp WINDOWS_EXE= && \
	  make -C ../software/app'

# ── Step 1: generate the .qsys file from the TCL script ──────────────────────
$(QSYS_FILE): $(QSYS_TCL)
	$(DOCKER) "$(DOCKER_CWD) && cd ../qsys && qsys-script --script=nios2_system.tcl"

qsys: $(QSYS_FILE)
	$(DOCKER) "$(DOCKER_CWD) && qsys-generate $(QSYS_FILE) --synthesis=VHDL --output-directory=$(QSYS_GEN_DIR)"

# ── Step 2: create and compile the Quartus project ───────────────────────────
project: $(QSYS_GEN_DIR)/synthesis/nios2_system.qip
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
	docker run --rm --privileged \
	  -v /dev/bus/usb:/dev/bus/usb \
	  -v $(REPO_ROOT):/work \
	  $(DOCKER_IMAGE) \
	  bash -c 'jtagd && sleep 2 && \
	    quartus_pgm -m jtag -o "p;/work/$(PROJECT_NAME)/quartus/$(REVISION_NAME).sof@$(DEVICE_INDEX)"; \
	    kill $$(pgrep jtagd) 2>/dev/null || true'

# download-elf: Downloads the Nios II ELF to the soft-core via JTAG and starts it.
#   The uname_shim prevents nios2-download from detecting WSL inside Docker.
download-elf: usb-wsl $(APP_DIR)/nios2_led.elf
	docker run --rm --privileged \
	  -v $(REPO_ROOT):/work \
	  -v $(UNAME_SHIM):/usr/local/bin/uname:ro \
	  $(DOCKER_IMAGE) \
	  nios2-download -g /work/04_nios2_led/software/app/nios2_led.elf

# terminal: Opens the Nios II JTAG UART terminal (Ctrl-C to quit).
terminal: usb-wsl
	docker run --rm -it --privileged \
	  -v $(UNAME_SHIM):/usr/local/bin/uname:ro \
	  $(DOCKER_IMAGE) \
	  nios2-terminal

# ── GUI targets ───────────────────────────────────────────────────────────────
# Opens a GUI tool inside Docker with X11 forwarding to the WSL2 host.
# Requires WSLg (Windows 11) or a running X server (DISPLAY must be set).

# quartus-gui: Opens Quartus Prime with the project loaded.
#   Run 'make project' first if de10_nano.qpf does not yet exist.
quartus-gui:
	$(DOCKER_GUI) "$(DOCKER_CWD) && quartus $(REVISION_NAME).qpf 2>/dev/null || quartus"

# platform-designer: Opens Platform Designer with the .qsys system file.
#   Run 'make $(QSYS_FILE)' first if the .qsys file does not yet exist.
platform-designer:
	$(DOCKER_GUI) "$(DOCKER_CWD) && qsys-edit $(QSYS_FILE) 2>/dev/null || qsys-edit"

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

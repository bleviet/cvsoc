# Makefile for the 08_nios2_debug Quartus project.
# Orchestrates: Platform Designer generation → Quartus compile → BSP generation → app build.
# Adds GDB server and interactive debug targets on top of the 06_nios2_interrupts flow.
#
# All steps run inside the cvsoc/quartus:23.1 Docker container.
# Call any target directly from the WSL2 host — Docker is invoked automatically.

PROJECT_NAME  = 08_nios2_debug
REVISION_NAME = de10_nano

QSYS_TCL      = ../qsys/nios2_system.tcl
QSYS_FILE     = ../qsys/nios2_system.qsys
QSYS_GEN_DIR  = ../qsys/nios2_system_gen
SOPCINFO      = ../qsys/nios2_system.sopcinfo

BSP_DIR       = ../software/bsp
APP_DIR       = ../software/app

DOCKER_IMAGE ?= cvsoc/quartus:23.1

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

# Nios II GDB server port
GDB_PORT     ?= 2345

.PHONY: all qsys project compile bsp app clean check_timing \
        program-sof download-elf terminal \
        gdb-server gdb \
        usb-windows usb-wsl \
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

# ── Timing check ──────────────────────────────────────────────────────────────
check_timing:
	$(DOCKER) "$(DOCKER_CWD) && python /work/00_led_blinking/scripts/check_timing_slacks.py $(REVISION_NAME).sta.rpt 2>/dev/null || true"

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

# ── Download ELF (run without debugger) ──────────────────────────────────────
download-elf: usb-wsl $(APP_DIR)/nios2_debug.elf
	docker run --rm --privileged \
	  -v $(REPO_ROOT):/work \
	  -v $(UNAME_SHIM):/usr/local/bin/uname:ro \
	  $(DOCKER_IMAGE) \
	  nios2-download -g /work/08_nios2_debug/software/app/nios2_debug.elf

# ── JTAG UART terminal ────────────────────────────────────────────────────────
terminal: usb-wsl
	docker run --rm -it --privileged \
	  -v $(UNAME_SHIM):/usr/local/bin/uname:ro \
	  $(DOCKER_IMAGE) \
	  nios2-terminal

# ── GDB server (start nios2-gdb-server, keep running for GDB to connect) ─────
#
# Run this in one terminal, then run 'make gdb' in a second terminal.
# The server resets the Nios II CPU and waits for a GDB connection on
# localhost:$(GDB_PORT).
#
# NOTE: nios2-gdb-server requires jtagd to be running inside the container.
#       The shim disables WSL detection inside Docker on WSL2 hosts.
gdb-server: usb-wsl
	docker run --rm -it --privileged \
	  -p $(GDB_PORT):$(GDB_PORT) \
	  -v $(REPO_ROOT):/work \
	  -v $(UNAME_SHIM):/usr/local/bin/uname:ro \
	  $(DOCKER_IMAGE) \
	  bash -c '/opt/intelFPGA/quartus/bin/jtagd && sleep 2 && \
	    nios2-gdb-server \
	      --tcpport $(GDB_PORT) \
	      --tcppersist \
	      --tcpdebug; \
	    kill $$(pgrep jtagd) 2>/dev/null || true'

# ── Interactive GDB session ───────────────────────────────────────────────────
#
# Connects nios2-elf-gdb to the running gdb-server (started with 'make gdb-server').
# The GDB init script loads the ELF, sets a breakpoint at set_led(), and continues.
#
# Usage:
#   Terminal 1: make gdb-server
#   Terminal 2: make gdb
gdb: $(APP_DIR)/nios2_debug.elf
	docker run --rm -it \
	  --network host \
	  -v $(REPO_ROOT):/work \
	  $(DOCKER_IMAGE) \
	  nios2-elf-gdb \
	    -ex "set pagination off" \
	    -x /work/08_nios2_debug/scripts/nios2_debug.gdb \
	    /work/08_nios2_debug/software/app/nios2_debug.elf

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
	  rm -rf db incremental_db output_files && \
	  rm -rf $(QSYS_GEN_DIR) $(QSYS_FILE) && \
	  rm -rf $(BSP_DIR) && \
	  make -C $(APP_DIR) clean 2>/dev/null || true && \
	  find . -maxdepth 1 \( -name '*.rpt' -o -name '*.summary' -o -name '*.qsf' \
	    -o -name '*.qpf' -o -name '*.qws' -o -name '*.done' -o -name '*.smsg' \
	    -o -name '*.jdi' -o -name '*.pin' -o -name '*.sld' -o -name '*.sof' \
	    -o -name 'dump.txt' \) -delete"

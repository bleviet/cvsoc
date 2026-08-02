# Makefile for the 08_nios2_debug Quartus project.
# Reuses the 06_nios2_interrupts hardware + BSP; adds GDB server and
# interactive debug targets on top of that flow.
# Nios II classic — supported only on Quartus 23.1 (Docker).

PROJECT_NAME  = 08_nios2_debug
REVISION_NAME = de10_nano

QSYS_TCL     = ../qsys/nios2_system.tcl
QSYS_FILE    = ../qsys/nios2_system.qsys
QSYS_GEN_DIR = ../qsys/nios2_system_gen
SOPCINFO     = ../qsys/nios2_system.sopcinfo
BSP_DIR      = ../software/bsp
APP_DIR      = ../software/app

QUARTUS_SUPPORTED ?= 23.1
QUARTUS_DEFAULT   ?= 23.1
NEEDS_SOFTWARE_TOOLS ?= 0

include ../../common/make/quartus-version.mk

UNAME_SHIM = $(REPO_ROOT)/common/docker/uname_shim.sh

# Nios II GDB server port
GDB_PORT     ?= 2345

.PHONY: all qsys project compile bsp app check_timing \
        program-sof download-elf terminal usb-wsl usb-windows \
        gdb-server gdb clean

# all: reuses Phase 4 (06_nios2_interrupts) hardware + BSP, then copies the
# bitstream/BSP into this project and builds the debug application.
all:
	@echo "═══════════════════════════════════════════════════════════════"
	@echo "  08_nios2_debug reuses 06_nios2_interrupts hardware + BSP"
	@echo "═══════════════════════════════════════════════════════════════"
	$(MAKE) -C $(REPO_ROOT)/06_nios2_interrupts/quartus all
	@echo "Copying bitstream and BSP from 06_nios2_interrupts..."
	$(call QTOOL, \
	  mkdir -p /work/$(PROJECT_NAME)/software/bsp /work/$(PROJECT_NAME)/software/app && \
	  rm -f /work/$(PROJECT_NAME)/quartus/$(REVISION_NAME).sof && \
	  cp /work/06_nios2_interrupts/quartus/de10_nano.sof /work/$(PROJECT_NAME)/quartus/$(REVISION_NAME).sof && \
	  rm -rf /work/$(PROJECT_NAME)/software/bsp/* && \
	  cp -r /work/06_nios2_interrupts/software/bsp/* /work/$(PROJECT_NAME)/software/bsp/ && \
	  make -C /work/$(PROJECT_NAME)/software/app)

$(QSYS_FILE): $(QSYS_TCL)
	$(call QTOOL,cd ../qsys && qsys-script --script=nios2_system.tcl)

qsys: $(QSYS_FILE)
	$(call QTOOL,qsys-generate $(QSYS_FILE) --synthesis=VHDL --output-directory=$(QSYS_GEN_DIR))

project: $(QSYS_GEN_DIR)/synthesis/nios2_system.qip
	$(call QTOOL,quartus_sh -t de10_nano_project.tcl)

compile:
	$(call QTOOL,quartus_sh --flow compile $(PROJECT_NAME) -c $(REVISION_NAME))

BSP_DEFAULT_TCL = /opt/intelFPGA/nios2eds/sdk2/bin/bsp-set-defaults.tcl

bsp: $(SOPCINFO)
	$(call QTOOL, \
	  mkdir -p $(BSP_DIR) && \
	  nios2-bsp-create-settings \
	    --sopc $(SOPCINFO) --type hal \
	    --settings $(BSP_DIR)/settings.bsp --bsp-dir $(BSP_DIR) \
	    --script $(BSP_DEFAULT_TCL) --cpu-name nios2 && \
	  make -C $(BSP_DIR) WINDOWS_EXE=)

app: bsp
	$(call QTOOL,make -C $(APP_DIR))

check_timing:
	$(call QTOOL,python3 $(QROOT)/00_led_blinking/scripts/check_timing_slacks.py \
	  $(REVISION_NAME).sta.rpt 2>/dev/null || true)

program-sof: usb-wsl $(REVISION_NAME).sof
	$(QUARTUS_PROGRAM)

# download-elf / terminal / gdb-server need the uname shim and
# --privileged / -it mounts that QTOOL does not provide; they are written
# as raw docker run commands.
download-elf: usb-wsl $(APP_DIR)/nios2_debug.elf
	docker run --rm --user $$(id -u):$$(id -g) --privileged \
	  -v $(REPO_ROOT):/work \
	  -v $(UNAME_SHIM):/usr/local/bin/uname:ro \
	  $(DOCKER_IMAGE) \
	  nios2-download -g /work/$(PROJECT_NAME)/software/app/nios2_debug.elf

terminal: usb-wsl
	docker run --rm --user $$(id -u):$$(id -g) -it --privileged \
	  -v $(UNAME_SHIM):/usr/local/bin/uname:ro \
	  $(DOCKER_IMAGE) \
	  nios2-terminal

# ── GDB server ────────────────────────────────────────────────────────────────
# Run this in one terminal, then run 'make gdb' in a second terminal.
# The server resets the Nios II CPU and waits for a GDB connection on
# localhost:$(GDB_PORT).  nios2-gdb-server requires jtagd to be running
# inside the container; the shim disables WSL detection inside Docker.
gdb-server: usb-wsl
	docker run --rm --user $$(id -u):$$(id -g) -it --privileged \
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
# Connects nios2-elf-gdb to the running gdb-server (started with 'make gdb-server').
# The GDB init script loads the ELF, sets a breakpoint at set_led(), and continues.
gdb: $(APP_DIR)/nios2_debug.elf
	docker run --rm --user $$(id -u):$$(id -g) -it \
	  --network host \
	  -v $(REPO_ROOT):/work \
	  $(DOCKER_IMAGE) \
	  nios2-elf-gdb \
	    -ex "set pagination off" \
	    -x /work/$(PROJECT_NAME)/scripts/nios2_debug.gdb \
	    /work/$(PROJECT_NAME)/software/app/nios2_debug.elf

clean:
	$(call QTOOL, \
	  rm -rf db incremental_db output_files && \
	  rm -rf $(QSYS_GEN_DIR) $(QSYS_FILE) && \
	  rm -rf $(BSP_DIR) && \
	  make -C $(APP_DIR) clean 2>/dev/null || true && \
	  find . -maxdepth 1 \( -name '*.rpt' -o -name '*.summary' -o -name '*.qsf' \
	    -o -name '*.qpf' -o -name '*.qws' -o -name '*.done' -o -name '*.smsg' \
	    -o -name '*.jdi' -o -name '*.pin' -o -name '*.sld' -o -name '*.sof' \
	    -o -name 'dump.txt' \) -delete)

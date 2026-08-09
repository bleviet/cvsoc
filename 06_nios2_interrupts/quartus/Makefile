# Makefile for the 06_nios2_interrupts Quartus project.
# Nios II classic — supported only on Quartus 23.1 (Docker).
# Orchestrates: Platform Designer → Quartus compile → BSP → app build.

PROJECT_NAME  = 06_nios2_interrupts
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

# Nios II GDB server port (nios2-gdb-server listens here; GDB clients connect to it)
GDB_PORT     ?= 2345

.PHONY: all qsys project compile bsp app check_timing \
        program-sof download-elf terminal usb-wsl usb-windows \
        close-gdb-port gdb-server gdb-tui clean

all:
	$(call QTOOL, \
	  cd ../qsys && qsys-script --script=nios2_system.tcl && \
	  cd ../quartus && \
	  qsys-generate ../qsys/nios2_system.qsys --synthesis=VHDL --output-directory=../qsys/nios2_system_gen && \
	  quartus_sh -t de10_nano_project.tcl && \
	  quartus_sh --flow compile $(PROJECT_NAME) -c $(REVISION_NAME) && \
	  mkdir -p ../software/bsp && \
	  nios2-bsp-create-settings --sopc ../qsys/nios2_system.sopcinfo --type hal \
	    --settings ../software/bsp/settings.bsp --bsp-dir ../software/bsp \
	    --script /opt/intelFPGA/nios2eds/sdk2/bin/bsp-set-defaults.tcl --cpu-name nios2 && \
	  make -C ../software/bsp WINDOWS_EXE= && \
	  make -C ../software/app)

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

program-sof: usb-ready $(REVISION_NAME).sof
	$(QUARTUS_PROGRAM)

# download-elf / terminal / gdb-server need the uname shim and
# --privileged / -it mounts that QTOOL does not provide; they are written
# as raw docker run commands.
download-elf: usb-ready $(APP_DIR)/nios2_interrupts.elf
	docker run --rm --user $$(id -u):$$(id -g) --privileged \
	  -v $(REPO_ROOT):/work \
	  -v $(UNAME_SHIM):/usr/local/bin/uname:ro \
	  $(DOCKER_IMAGE) \
	  nios2-download -g /work/$(PROJECT_NAME)/software/app/nios2_interrupts.elf

terminal: usb-ready
	docker run --rm --user $$(id -u):$$(id -g) -it --privileged \
	  -v $(UNAME_SHIM):/usr/local/bin/uname:ro \
	  $(DOCKER_IMAGE) \
	  nios2-terminal

# ── GDB server ────────────────────────────────────────────────────────────────
# close-gdb-port: Stops any running Docker container that currently publishes
#   localhost:$(GDB_PORT), so gdb-server can bind the port reliably.
close-gdb-port:
	@PORT_CONTAINERS=$$(docker ps --format '{{.ID}} {{.Ports}}' | \
	  grep -E "(0\\.0\\.0\\.0|\\[::\\]):$(GDB_PORT)->$(GDB_PORT)/tcp" | \
	  awk '{print $$1}'); \
	if [ -n "$$PORT_CONTAINERS" ]; then \
	  echo "Stopping container(s) using port $(GDB_PORT): $$PORT_CONTAINERS"; \
	  docker stop $$PORT_CONTAINERS >/dev/null; \
	else \
	  echo "Port $(GDB_PORT) is free (no Docker container bound)."; \
	fi

# Starts nios2-gdb-server and keeps it running so a GDB client can connect.
# Run this in one terminal, then run 'make gdb-tui' in a second terminal.
# The server resets the Nios II CPU and waits for a connection on
# localhost:$(GDB_PORT).  --tcppersist keeps it alive after GDB disconnects.
gdb-server: close-gdb-port usb-ready
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

# ── GDB TUI session ───────────────────────────────────────────────────────────
# Opens nios2-elf-gdb in TUI mode (source + assembly split view).
# Requires gdb-server to be running first (make gdb-server in another terminal).
gdb-tui: $(APP_DIR)/nios2_interrupts.elf
	docker run --rm -it \
	  --network host \
	  -e TERM=$(TERM) \
	  -v $(REPO_ROOT):/work \
	  $(DOCKER_IMAGE) \
	  nios2-elf-gdb -tui \
	    -ex "set pagination off" \
	    -x /work/$(PROJECT_NAME)/scripts/nios2_interrupts.gdb \
	    /work/$(PROJECT_NAME)/software/app/nios2_interrupts.elf

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
